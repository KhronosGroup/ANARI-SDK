// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>

#include "material.h"
#include "meshUtil.h"
#include "renderParam.h"

// pxr
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/sceneDelegate.h>

// std
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMesh final : public HdMesh
{
  HF_MALLOC_TAG_NEW("new HdAnariMesh");

  HdAnariMesh(anari::Device d,
      const SdfPath &id,
      const SdfPath &instancerId = SdfPath());
  ~HdAnariMesh() override;

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  void Finalize(HdRenderParam *renderParam) override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits,
      const TfToken &reprToken) override;

  void AddInstances(std::vector<anari::Instance> &instances) const;

 private:

  void _UpdatePrimvarSources(
      HdSceneDelegate *sceneDelegate, HdAnariRenderParam *renderParams, HdDirtyBits dirtyBits, const HdPrimvarDescriptorVector& primvarDescriptors, HdAnariMaterial::PrimvarBinding primvarBinding);

  void _SetGeometryAttributeConstant(const TfToken& attrName, VtValue v) const ;
  void _SetGeometryAttributeArray(const TfToken& attrName, const TfToken& pvname, HdInterpolation hdinterpolation, VtValue v) const;

  void _ReleaseAnariInstances();

  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
  void _InitRepr(const TfToken &reprToken, HdDirtyBits *dirtyBits) override;

  HdAnariMesh(const HdAnariMesh &) = delete;
  HdAnariMesh &operator=(const HdAnariMesh &) = delete;


  // Data //

  bool _populated{false};
  HdMeshTopology _topology;
  std::unique_ptr<HdAnariMeshUtil> _meshUtil;

  VtIntArray _trianglePrimitiveParams;

  struct AnariObjects
  {
    anari::Device device{nullptr};
    anari::Geometry geometry{nullptr};
    anari::Surface surface{nullptr};
    anari::Group group{nullptr};
    std::vector<anari::Instance> instances;
  } _anari;
};

PXR_NAMESPACE_CLOSE_SCOPE