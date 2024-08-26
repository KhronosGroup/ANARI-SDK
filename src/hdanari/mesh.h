// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>

#include "material.h"
#include "meshUtil.h"
#include "renderParam.h"

// pxr
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/vertexAdjacency.h>

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
  void _UpdatePrimvarSources(HdSceneDelegate *sceneDelegate, const HdPrimvarDescriptorVector& primvarDescriptors, HdAnariMaterial::PrimvarBinding primvarBinding);
  void _UpdateComputationPrimvarSources(HdSceneDelegate *sceneDelegate, const HdExtComputationPrimvarDescriptorVector& computationPrimvarDescriptors, const HdExtComputationUtils::ValueStore& valueStore, HdAnariMaterial::PrimvarBinding primvarBinding);

  void _SetGeometryAttributeConstant(const TfToken& attributeName, const VtValue& v);
  void _SetGeometryAttributeArray(const TfToken& attributeName, const TfToken& bindingPoint, const VtValue& v);
#if USE_INSTANCE_ARRAYS
  void _SetInstanceAttributeArray(const TfToken& attributeName, const VtValue& v);
#endif

  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
  void _InitRepr(const TfToken &reprToken, HdDirtyBits *dirtyBits) override;

#if !USE_INSTANCE_ARRAYS
  void ReleaseInstances();
#endif

  HdAnariMesh(const HdAnariMesh &) = delete;
  HdAnariMesh &operator=(const HdAnariMesh &) = delete;


  // Data //

  bool _populated{false};
  HdMeshTopology topology_;
  std::unique_ptr<HdAnariMeshUtil> meshUtil_;
  std::optional<Hd_VertexAdjacency> adjacency_;

  VtIntArray trianglePrimitiveParams_;

  HdAnariMaterial::PrimvarBinding primvarBinding_;
  std::map<TfToken, TfToken> geometryBindingPoints_;
  std::map<TfToken, TfToken> instanceBindingPoints_;


  struct AnariObjects
  {
    anari::Device device{nullptr};
    anari::Geometry geometry{nullptr};
    anari::Surface surface{nullptr};
    anari::Group group{nullptr};
#if USE_INSTANCE_ARRAYS
    anari::Instance instance{nullptr};
#else
    std::vector<anari::Instance> instances;
#endif
  } _anari;
};

PXR_NAMESPACE_CLOSE_SCOPE