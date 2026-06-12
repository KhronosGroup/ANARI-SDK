// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mesh.h"

#include "anariTokens.h"
#include "geometry.h"
#include "renderParam.h"
// anari
#include <anari/anari.h>
#include <anari/frontend/anari_enums.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
// pxr
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/geomSubset.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
// std
#include <cassert>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

// HdAnariPoints definitions
// ////////////////////////////////////////////////////

HdAnariMesh::HdAnariMesh(
    anari::Device d, const SdfPath &id, const SdfPath &instancerId)
    : HdAnariGeometry(d, HdAnariTokens->triangle, id, instancerId)
{}

HdDirtyBits HdAnariMesh::GetInitialDirtyBitsMask() const
{
  return HdChangeTracker::Clean | HdChangeTracker::InitRepr
      | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology
      | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility
      | HdChangeTracker::DirtyCullStyle | HdChangeTracker::DirtyDoubleSided
      | HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtySubdivTags
      | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyNormals
      | HdChangeTracker::DirtyInstancer | HdChangeTracker::DirtyPrimID
      | HdChangeTracker::DirtyRepr | HdChangeTracker::DirtyMaterialId;
}

void HdAnariMesh::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam_,
    HdDirtyBits *dirtyBits,
    const TfToken &reprToken)
{
  auto renderParam = static_cast<HdAnariRenderParam *>(renderParam_);

  if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())) {
    auto rp = static_cast<HdAnariRenderParam *>(renderParam_);

    topology_ = HdMeshTopology(GetMeshTopology(sceneDelegate), 0);
    meshUtil_ = std::make_unique<HdAnariMeshUtil>(&topology_, GetId());

    meshUtil_->ComputeTriangleIndices(
        &triangulatedIndices_, &trianglePrimitiveParams_);

    // Create transient mapping structure to be able to map a face to its
    // subdivised triangles. To be used to build geomsubset topology.
    VtIntArray trianglesCounts;

    auto count = 1ull;
    auto prev = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(
        trianglePrimitiveParams_.front());
    for (auto i = 1ull; i < trianglePrimitiveParams_.size(); ++i) {
      auto pp = trianglePrimitiveParams_[i];
      int faceIndex = HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(pp);
      if (faceIndex != prev) {
        trianglesCounts.push_back(count);
        prev = faceIndex;
        count = 1;
      } else {
        ++count;
      }
    }
    trianglesCounts.push_back(count);

    VtIntArray trianglesStarts(1, 0);
    for (auto i = 0; i < trianglesCounts.size(); ++i) {
      trianglesStarts.push_back(trianglesStarts.back() + trianglesCounts[i]);
    }

    // Build topology/triangle sets for each subgeom.
    // Release any previously owned anari array before assigning the new one.
    geomsubsets_ = topology_.GetGeomSubsets();
    if (geomsubsets_.empty()) {
      if (auto trianglesIt = geomSubsetTriangles_.find(GetId());
          trianglesIt != cend(geomSubsetTriangles_)) {
        anari::release(device_, trianglesIt->second);
        geomSubsetTriangles_.erase(trianglesIt);
      }

      if (!triangulatedIndices_.empty()) {
        geomSubsetTriangles_[GetId()] = _GetAttributeArray(
            VtValue(triangulatedIndices_), ANARI_UINT32_VEC3);
      }
    } else {
      // We go a slightly different path here.
      // In order to be able to share faceVarying and face primvars between
      // subsets, we generate degenerate triangles for triangles not belonging
      // to the subset, so all topology have the same size. Seed from the first
      // index; when the mesh triangulates to nothing the seed is unused (the
      // built index arrays are empty), so avoid indexing an empty array.
      const GfVec3i degenerate = triangulatedIndices_.empty()
          ? GfVec3i(0)
          : GfVec3i(triangulatedIndices_[trianglesStarts[0]][0]);

      // Track which faces a subset claims so we can render the remainder (faces
      // covered by no subset) with the prim's own material below.
      std::vector<bool> faceCovered(trianglesCounts.size(), false);

      auto buildTriangles = [&](const auto &faceIndices) {
        VtVec3iArray indices(triangulatedIndices_.size(), degenerate);
        for (auto faceIndex : faceIndices) {
          auto trianglesStart = trianglesStarts[faceIndex];
          auto trianglesCount = trianglesCounts[faceIndex];
          for (auto t = 0; t < trianglesCount; ++t) {
            indices[trianglesStart + t] =
                triangulatedIndices_[trianglesStart + t];
          }
        }
        return _GetAttributeArray(VtValue(indices), ANARI_UINT32_VEC3);
      };

      for (auto subset : geomsubsets_) {
        for (auto faceIndex : subset.indices)
          faceCovered[faceIndex] = true;
        geomSubsetTriangles_[subset.id] = buildTriangles(subset.indices);
      }

      // A geomsubset partition need not cover every face. Faces left over use
      // the prim's own material; without this they would never be rendered.
      if (auto it = geomSubsetTriangles_.find(GetId());
          it != cend(geomSubsetTriangles_)) {
        anari::release(device_, it->second);
        geomSubsetTriangles_.erase(it);
      }
      VtIntArray remainderFaces;
      for (size_t faceIndex = 0; faceIndex < faceCovered.size(); ++faceIndex) {
        if (!faceCovered[faceIndex])
          remainderFaces.push_back(int(faceIndex));
      }
      if (!remainderFaces.empty())
        geomSubsetTriangles_[GetId()] = buildTriangles(remainderFaces);
    }
  }

  // Call the main geometry sync.
  HdAnariGeometry::Sync(sceneDelegate, renderParam_, dirtyBits, reprToken);
}

bool HdAnariMesh::HasMainGeometry() const
{
  // Present either when there are no subsets (full triangulation) or when a
  // subset partition leaves uncovered faces (the remainder triangle set).
  return geomSubsetTriangles_.count(GetId()) > 0;
}

HdGeomSubsets HdAnariMesh::GetGeomSubsets(
    HdSceneDelegate *sceneDelegate, HdDirtyBits *dirtyBits)
{
  return geomsubsets_;
}

// Per-corner (faceVarying) normals with crease splitting: a vertex's incident
// face normals are accumulated only within an angle threshold, so smooth
// regions stay smooth (sphere) while sharp features keep hard edges (cube
// edges, cone/cylinder caps). USD ships no normals for implicit surfaces, and
// plain adjacency averaging flattens capped shapes by blending the cap and
// side normals into their shared rim vertices.
static constexpr float kCosCreaseThreshold = 0.342f; // cos(70 degrees)

static GfVec3f _SafeNormalize(const GfVec3f &v)
{
  const float len = v.GetLength();
  return len > 0.0f ? v / len : GfVec3f(0.0f);
}

static VtVec3fArray _ComputeCreaseNormals(
    const VtVec3fArray &points, const VtVec3iArray &triangles)
{
  const size_t numTris = std::size(triangles);

  std::vector<GfVec3f> faceNormal(numTris); // normalized, for the angle test
  std::vector<GfVec3f> faceWeighted(numTris); // area-weighted, for accumulation
  for (size_t t = 0; t < numTris; ++t) {
    const GfVec3i &tri = triangles[t];
    const GfVec3f &p0 = points[tri[0]];
    const GfVec3f cross = GfCross(points[tri[1]] - p0, points[tri[2]] - p0);
    faceWeighted[t] = cross; // length is proportional to twice the area
    faceNormal[t] = _SafeNormalize(cross);
  }

  std::vector<std::vector<int>> incidentTriangles(std::size(points));
  for (size_t t = 0; t < numTris; ++t)
    for (int c = 0; c < 3; ++c)
      incidentTriangles[triangles[t][c]].push_back(int(t));

  VtVec3fArray normals(3 * numTris);
  for (size_t t = 0; t < numTris; ++t) {
    for (int c = 0; c < 3; ++c) {
      GfVec3f acc(0.0f);
      for (int other : incidentTriangles[triangles[t][c]]) {
        if (GfDot(faceNormal[t], faceNormal[other]) >= kCosCreaseThreshold)
          acc += faceWeighted[other];
      }
      const GfVec3f n = _SafeNormalize(acc);
      normals[3 * t + c] = (n == GfVec3f(0.0f)) ? faceNormal[t] : n;
    }
  }
  return normals;
}

HdAnariGeometry::GeomSpecificPrimvars HdAnariMesh::GetGeomSpecificPrimvars(
    HdSceneDelegate *sceneDelegate,
    HdDirtyBits *dirtyBits,
    const TfToken::Set &allPrimvars,
    const VtVec3fArray &points,
    const SdfPath &geomsetId)
{
  GeomSpecificPrimvars primvars;

  // Topology
  if (geomsubsets_.empty()) {
    // Main topology only if no geomsubset is present
    primvars.push_back(
        {HdAnariTokens->primitiveIndex, geomSubsetTriangles_[GetId()]});
  } else {
    primvars.push_back(
        {HdAnariTokens->primitiveIndex, geomSubsetTriangles_[geomsetId]});
  }

  // When the prim authors no normals, synthesize crease-aware per-corner
  // normals. Implicit surfaces (sphere, cube, cone, cylinder, capsule, plane)
  // arrive with none, and so do polygonal meshes that omit them.
  const bool normalIsAuthored =
      allPrimvars.find(HdTokens->normals) != std::cend(allPrimvars);
  if (!normalIsAuthored) {
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())
        || HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, GetId(), HdTokens->points)) {
      anari::release(device_, normals_);
      normals_ = (std::size(points) > 0 && !triangulatedIndices_.empty())
          ? _GetAttributeArray(
                VtValue(_ComputeCreaseNormals(points, triangulatedIndices_)))
          : anari::Array1D{};
    }
    if (normals_)
      primvars.push_back({HdAnariTokens->faceVaryingNormal, normals_});
  } else {
    anari::release(device_, normals_);
    normals_ = {};
  }

  return primvars;
}

HdAnariGeometry::PrimvarSource HdAnariMesh::UpdatePrimvarSource(
    HdSceneDelegate *sceneDelegate,
    HdInterpolation interpolation,
    const TfToken &attributeName,
    const VtValue &value)
{
  switch (interpolation) {
  case HdInterpolationConstant: {
    if (value.IsArrayValued()) {
      if (value.GetArraySize() == 0) {
        TF_RUNTIME_ERROR("Constant interpolation with no value.");
        return {};
      }
      if (value.GetArraySize() > 1) {
        TF_RUNTIME_ERROR("Constant interpolation with more than one value.");
      }
    }

    GfVec4f v;
    if (_GetVtValueAsAttribute(value, v)) {
      return v;
    } else {
      TF_RUNTIME_ERROR(
          "Error extracting value from primvar %s", attributeName.GetText());
    }
    break;
  }
  case HdInterpolationUniform: {
    VtValue perFace;
    meshUtil_->GatherPerFacePrimvar(
        GetId(), attributeName, value, trianglePrimitiveParams_, &perFace);
    return _GetAttributeArray(perFace);
  }
  case HdInterpolationFaceVarying: {
    HdVtBufferSource buffer(attributeName, value);
    VtValue triangulatedPrimvar;
    auto result =
        meshUtil_->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

#if PXR_VERSION >= 2511
    if (result == HdMeshComputationResult::Success) {
      return _GetAttributeArray(triangulatedPrimvar);
    } else if (result == HdMeshComputationResult::Unchanged) {
      return _GetAttributeArray(value);
    } else {
      TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
      return {};
    }
#else
    if (result) {
      return _GetAttributeArray(triangulatedPrimvar);
    } else {
      TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
      return {};
    }
#endif
    break;
  }
  case HdInterpolationVarying:
  case HdInterpolationVertex: {
    return _GetAttributeArray(value);
  }
  case HdInterpolationInstance:
  case HdInterpolationCount:
    assert(false);
    break;
  }

  return {};
}

void HdAnariMesh::Finalize(HdRenderParam *renderParam_)
{
  anari::release(device_, normals_);
  for (auto &pair : geomSubsetTriangles_) {
    anari::release(device_, pair.second);
  }

  HdAnariGeometry::Finalize(renderParam_);
}

PXR_NAMESPACE_CLOSE_SCOPE
