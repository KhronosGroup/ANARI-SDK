// Copyright 2024-2025 The Khronos Group
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
#include <pxr/imaging/hd/smoothNormals.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/imaging/pxOsd/tokens.h>
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
    adjacency_.reset();

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
      // to the subset, so all topology have the same size.
      for (auto subset : geomsubsets_) {
        auto id = subset.id;

        // Fill with a degenerate triangle, that is using vertices from the
        // actual geomsubset so its extent stays valid.
        VtVec3iArray indices(triangulatedIndices_.size(),
            GfVec3i(triangulatedIndices_[trianglesStarts[0]][0]));

        for (auto i : subset.indices) {
          auto trianglesStart = trianglesStarts[i];
          auto trianglesCount = trianglesCounts[i];

          for (auto i = 0; i < trianglesCount; ++i) {
            indices[trianglesStart + i] =
                triangulatedIndices_[trianglesStart + i];
          }
        }

        auto subsetTriangles =
            _GetAttributeArray(VtValue(indices), ANARI_UINT32_VEC3);
        geomSubsetTriangles_[id] = subsetTriangles;
      }
    }
  }

  // Call the main geometry sync.
  HdAnariGeometry::Sync(sceneDelegate, renderParam_, dirtyBits, reprToken);
}

HdGeomSubsets HdAnariMesh::GetGeomSubsets(
    HdSceneDelegate *sceneDelegate, HdDirtyBits *dirtyBits)
{
  return geomsubsets_;
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

  // Normals smoothing if needed.
  const auto &normals = sceneDelegate->Get(GetId(), HdTokens->normals);
  bool normalIsAuthored =
      allPrimvars.find(HdTokens->normals) != std::cend(allPrimvars);
  bool doSmoothNormals = (topology_.GetScheme() != PxOsdOpenSubdivTokens->none)
      && (topology_.GetScheme() != PxOsdOpenSubdivTokens->bilinear);

  if (!normalIsAuthored && doSmoothNormals) {
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())
        || HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, GetId(), HdTokens->points)) {
      if (!adjacency_.has_value()) {
        adjacency_.emplace();
        adjacency_->BuildAdjacencyTable(&topology_);
      }

      anari::release(device_, normals_);

      if (TF_VERIFY(std::size(points) > 0)) {
        const VtVec3fArray &normals = Hd_SmoothNormals::ComputeSmoothNormals(
            &*adjacency_, std::size(points), std::cbegin(points));
        normals_ = _GetAttributeArray(VtValue(normals));
        primvars.push_back({HdAnariTokens->vertexNormal, normals_});
      } else {
        normals_ = {};
      }
    }
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
    auto success =
        meshUtil_->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

    if (success) {
      return _GetAttributeArray(triangulatedPrimvar);
    } else {
      TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
      return {};
    }
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
