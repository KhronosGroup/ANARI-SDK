// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mesh.h"

#include <anari/frontend/anari_enums.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/smoothNormals.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>

#include <anari/anari_cpp.hpp>
#include <iterator>

#include "anariTokens.h"
#include "debugCodes.h"
#include "geometry.h"

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

HdAnariMaterial::PrimvarBinding HdAnariMesh::UpdateGeometry(
    HdSceneDelegate *sceneDelegate,
    HdDirtyBits *dirtyBits,
    const TfToken::Set &allPrimvars,
    const VtValue &points)
{
  // Triangle indices //
  if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())) {
    topology_ = HdMeshTopology(GetMeshTopology(sceneDelegate), 0);
    meshUtil_ = std::make_unique<HdAnariMeshUtil>(&topology_, GetId());
    adjacency_.reset();

    VtVec3iArray triangulatedIndices;
    VtIntArray trianglePrimitiveParams;
    meshUtil_->ComputeTriangleIndices(
        &triangulatedIndices, &trianglePrimitiveParams_);

    if (!triangulatedIndices.empty()) {
      _SetGeometryAttributeArray(HdAnariTokens->index,
          HdAnariTokens->primitiveIndex,
          VtValue(triangulatedIndices),
          ANARI_UINT32_VEC3);
    } else {
      _SetGeometryAttributeArray(
          HdAnariTokens->index, HdAnariTokens->primitiveIndex, VtValue());
    }
  }

  // Normals
  const auto &normals = sceneDelegate->Get(GetId(), HdTokens->normals);
  bool normalIsAuthored =
      allPrimvars.find(HdTokens->normals) != std::cend(allPrimvars);
  bool doSmoothNormals = (topology_.GetScheme() != PxOsdOpenSubdivTokens->none)
      && (topology_.GetScheme() != PxOsdOpenSubdivTokens->bilinear);

  if (normalIsAuthored) {
    // Just return the binding information so that it is part of the overall
    // primvar binding process.
    return {{HdTokens->normals, HdAnariTokens->normal}};
  } else if (doSmoothNormals) {
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())
        || HdChangeTracker::IsPrimvarDirty(
            *dirtyBits, GetId(), HdTokens->points)) {
      if (!adjacency_.has_value()) {
        adjacency_.emplace();
        adjacency_->BuildAdjacencyTable(&topology_);
      }

      if (TF_VERIFY(
              points.IsHolding<VtVec3fArray>() && points.GetArraySize() > 0)) {
        const auto &pointsArray = points.Get<VtVec3fArray>();
        const VtVec3fArray &normals = Hd_SmoothNormals::ComputeSmoothNormals(
            &*adjacency_, std::size(pointsArray), std::cbegin(pointsArray));

        _SetGeometryAttributeArray(HdAnariTokens->normal,
            HdAnariTokens->vertexNormal,
            VtValue(normals));
      } else {
        TF_RUNTIME_ERROR(
            "Cannot find a valid points source to compute normal for %s\n",
            GetId().GetText());
        _SetGeometryAttributeArray(
            HdAnariTokens->normal, HdAnariTokens->vertexNormal, VtValue());
      }
    }
  } else {
    _SetGeometryAttributeArray(
        HdAnariTokens->normal, HdAnariTokens->vertexNormal, VtValue());
  }

  return {};
}

void HdAnariMesh::UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
    HdInterpolation interpolation,
    const TfToken &attributeName,
    const VtValue &value)
{
  switch (interpolation) {
  case HdInterpolationConstant: {
    _SetGeometryAttributeConstant(attributeName, value);
    break;
  }
  case HdInterpolationUniform: {
    VtValue perFace;
    meshUtil_->GatherPerFacePrimvar(
        GetId(), attributeName, value, trianglePrimitiveParams_, &perFace);
    auto bindingPoint = _GetPrimitiveBindingPoint(attributeName);
    if (bindingPoint.IsEmpty()) {
      TF_CODING_ERROR("%s is not a valid per primitive geometry attribute\n",
          attributeName.GetText());
      break;
    }
    _SetGeometryAttributeArray(attributeName, bindingPoint, perFace);
    break;
  }
  case HdInterpolationFaceVarying: {
    auto bindingPoint = _GetFaceVaryingBindingPoint(attributeName);
    if (bindingPoint.IsEmpty()) {
      TF_CODING_ERROR("%s is not a valid faceVarying geometry attribute\n",
          attributeName.GetText());
      break;
    }

    HdVtBufferSource buffer(attributeName, value);
    VtValue triangulatedPrimvar;
    auto success =
        meshUtil_->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

    if (success) {
      _SetGeometryAttributeArray(
          attributeName, bindingPoint, triangulatedPrimvar);
    } else {
      TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
      _SetGeometryAttributeArray(attributeName, bindingPoint, VtValue());
    }
    break;
  }
  case HdInterpolationVarying:
  case HdInterpolationVertex: {
    auto bindingPoint = _GetVertexBindingPoint(attributeName);
    if (bindingPoint.IsEmpty()) {
      TF_CODING_ERROR("%s is not a valid vertex geometry attribute\n",
          attributeName.GetText());
      break;
    }
    _SetGeometryAttributeArray(attributeName, bindingPoint, value);
    break;
  }
  default:
    break;
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
