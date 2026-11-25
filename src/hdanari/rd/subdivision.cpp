// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "subdivision.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <opensubdiv/far/primvarRefiner.h>
#include <opensubdiv/far/topologyRefiner.h>

#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

namespace Far = OpenSubdiv::Far;

// PrimvarRefiner element wrapper. T is float or a GfVec*f; both support the
// scalar constructor (zero) and (vec * float) used here.
template <typename T>
struct _Element
{
  T value;
  void Clear()
  {
    value = T(0.0f);
  }
  void AddWithWeight(const _Element &src, float weight)
  {
    value += src.value * weight;
  }
};

enum class _Mode
{
  Vertex,
  Varying,
  Uniform
};

template <typename T>
VtArray<T> _RefineArray(const Far::TopologyRefiner &refiner,
    int level,
    _Mode mode,
    const VtArray<T> &coarse)
{
  Far::PrimvarRefiner primvarRefiner(refiner);

  // Uniform primvars carry one value per face; everything else, one per vertex.
  const bool perFace = mode == _Mode::Uniform;
  const int total =
      perFace ? refiner.GetNumFacesTotal() : refiner.GetNumVerticesTotal();

  std::vector<_Element<T>> buffer(total);
  for (size_t i = 0; i < coarse.size() && i < buffer.size(); ++i)
    buffer[i].value = coarse[i];

  _Element<T> *src = buffer.data();
  for (int l = 1; l <= level; ++l) {
    const Far::TopologyLevel &prev = refiner.GetLevel(l - 1);
    _Element<T> *dst =
        src + (perFace ? prev.GetNumFaces() : prev.GetNumVertices());
    switch (mode) {
    case _Mode::Vertex:
      primvarRefiner.Interpolate(l, src, dst);
      break;
    case _Mode::Varying:
      primvarRefiner.InterpolateVarying(l, src, dst);
      break;
    case _Mode::Uniform:
      primvarRefiner.InterpolateFaceUniform(l, src, dst);
      break;
    }
    src = dst;
  }

  const Far::TopologyLevel &last = refiner.GetLevel(level);
  const int count = perFace ? last.GetNumFaces() : last.GetNumVertices();
  VtArray<T> refined(count);
  for (int i = 0; i < count; ++i)
    refined[i] = src[i].value;
  return refined;
}

VtValue _RefineTyped(const Far::TopologyRefiner &refiner,
    int level,
    _Mode mode,
    const VtValue &v)
{
  if (v.IsHolding<VtVec3fArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtVec3fArray>()));
  if (v.IsHolding<VtVec2fArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtVec2fArray>()));
  if (v.IsHolding<VtVec4fArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtVec4fArray>()));
  if (v.IsHolding<VtFloatArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtFloatArray>()));
  // Double-precision variants: texcoords in particular can be authored double
  // (see _FlipTextureCoordinateV), so refine them rather than drop.
  if (v.IsHolding<VtVec3dArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtVec3dArray>()));
  if (v.IsHolding<VtVec2dArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtVec2dArray>()));
  if (v.IsHolding<VtVec4dArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtVec4dArray>()));
  if (v.IsHolding<VtDoubleArray>())
    return VtValue(
        _RefineArray(refiner, level, mode, v.UncheckedGet<VtDoubleArray>()));
  // Unsupported element type (e.g. integer ids): drop it.
  // Returning the coarse array unchanged would bind a vertex/varying attribute
  // shorter than the refined vertex count and read out of bounds at render
  // time.
  return VtValue();
}

} // namespace

std::unique_ptr<HdAnariSubdivision> HdAnariSubdivision::Create(
    const HdMeshTopology &topology, int refineLevel)
{
  if (refineLevel <= 0)
    return nullptr;
  if (topology.GetScheme() == PxOsdOpenSubdivTokens->none)
    return nullptr;
  if (!topology.GetGeomSubsets().empty())
    return nullptr;

  PxOsdTopologyRefinerSharedPtr refiner =
      PxOsdRefinerFactory::Create(topology.GetPxOsdMeshTopology());
  if (!refiner)
    return nullptr;

  Far::TopologyRefiner::UniformOptions options(refineLevel);
  options.fullTopologyInLastLevel = true;
  refiner->RefineUniform(options);

  const Far::TopologyLevel &last = refiner->GetLevel(refineLevel);
  const int numFaces = last.GetNumFaces();
  VtIntArray faceVertexCounts(numFaces);
  VtIntArray faceVertexIndices;
  faceVertexIndices.reserve(last.GetNumFaceVertices());
  for (int f = 0; f < numFaces; ++f) {
    const Far::ConstIndexArray verts = last.GetFaceVertices(f);
    faceVertexCounts[f] = verts.size();
    for (int i = 0; i < verts.size(); ++i)
      faceVertexIndices.push_back(verts[i]);
  }

  std::unique_ptr<HdAnariSubdivision> result(new HdAnariSubdivision);
  result->refiner_ = refiner;
  result->level_ = refineLevel;
  result->refinedTopology_ = HdMeshTopology(PxOsdOpenSubdivTokens->none,
      topology.GetOrientation(),
      faceVertexCounts,
      faceVertexIndices);
  return result;
}

VtValue HdAnariSubdivision::Refine(
    const VtValue &value, HdInterpolation interpolation) const
{
  if (!refiner_)
    return value;

  switch (interpolation) {
  case HdInterpolationConstant:
    return value; // one value for the whole prim, unchanged by refinement
  case HdInterpolationVertex:
    return _RefineTyped(*refiner_, level_, _Mode::Vertex, value);
  case HdInterpolationVarying:
    return _RefineTyped(*refiner_, level_, _Mode::Varying, value);
  case HdInterpolationUniform:
    return _RefineTyped(*refiner_, level_, _Mode::Uniform, value);
  default:
    // Face-varying and instance interpolation are not refined in v1.
    return VtValue();
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
