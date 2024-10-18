// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>

// pxr
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vertexAdjacency.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include "geometry.h"
#include "material.h"
#include "meshUtil.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariMesh final : public HdAnariGeometry
{
 public:
  HF_MALLOC_TAG_NEW("new HdAnariMesh");
  HdAnariMesh(anari::Device d,
      const SdfPath &id,
      const SdfPath &instancerId = SdfPath());

  HdDirtyBits GetInitialDirtyBitsMask() const override;

 protected:
  HdAnariMaterial::PrimvarBinding UpdateGeometry(HdSceneDelegate *sceneDelegate,
      HdDirtyBits *dirtyBits,
      const TfToken::Set &allPrimvars,
      const VtValue &points) override;
  void UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
      HdInterpolation interpolation,
      const TfToken &attributeName,
      const VtValue &value) override;

 private:
  HdAnariMesh(const HdAnariMesh &) = delete;
  HdAnariMesh &operator=(const HdAnariMesh &) = delete;

  HdMeshTopology topology_;
  std::unique_ptr<HdAnariMeshUtil> meshUtil_;
  std::optional<Hd_VertexAdjacency> adjacency_;

  VtIntArray trianglePrimitiveParams_;
};

PXR_NAMESPACE_CLOSE_SCOPE
