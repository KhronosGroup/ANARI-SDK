// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry.h"
#include "meshUtil.h"

// anari
#include <anari/anari_cpp.hpp>
// pxr
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/geomSubset.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vertexAdjacency.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
// std
#include <memory>
#include <optional>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMesh final : public HdAnariGeometry
{
 public:
  HF_MALLOC_TAG_NEW("new HdAnariMesh");
  HdAnariMesh(anari::Device d,
      const SdfPath &id,
      const SdfPath &instancerId = SdfPath());

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam_,
      HdDirtyBits *dirtyBits,
      const TfToken &reprToken) override;

  void Finalize(HdRenderParam *renderParam_) override;

 protected:
  HdGeomSubsets GetGeomSubsets(
      HdSceneDelegate *sceneDelegate, HdDirtyBits *dirtyBits) override;
  HdAnariGeometry::GeomSpecificPrimvars GetGeomSpecificPrimvars(
      HdSceneDelegate *sceneDelegate,
      HdDirtyBits *dirtyBits,
      const TfToken::Set &allPrimvars,
      const VtVec3fArray &points,
      const SdfPath &geomsetId = {}) override;
  HdAnariGeometry::PrimvarSource UpdatePrimvarSource(
      HdSceneDelegate *sceneDelegate,
      HdInterpolation interpolation,
      const TfToken &attributeName,
      const VtValue &value) override;

 private:
  HdAnariMesh(const HdAnariMesh &) = delete;
  HdAnariMesh &operator=(const HdAnariMesh &) = delete;

  HdMeshTopology topology_;
  std::unique_ptr<HdAnariMeshUtil> meshUtil_;
  std::optional<Hd_VertexAdjacency> adjacency_;

  HdGeomSubsets geomsubsets_;

  VtVec3iArray triangulatedIndices_;
  VtIntArray trianglePrimitiveParams_;
  anari::Array1D normals_{};

  std::unordered_map<SdfPath, anari::Array1D, SdfPath::Hash>
      geomSubsetTriangles_;
};

PXR_NAMESPACE_CLOSE_SCOPE
