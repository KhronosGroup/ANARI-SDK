// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>
// pxr
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/meshTopology.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/sphereSchema.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vertexAdjacency.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <map>
// std
#include <memory>
#include <optional>
#include <vector>

#include "material.h"
#include "meshUtil.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariGeometry : public HdMesh
{
  HF_MALLOC_TAG_NEW("new HdAnariGeometry");

 public:
  HdAnariGeometry(anari::Device d,
      const TfToken &geometryType,
      const SdfPath &id,
      const SdfPath &instancerId = SdfPath());
  ~HdAnariGeometry() override;

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  void Finalize(HdRenderParam *renderParam) override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits,
      const TfToken &reprToken) override;

  void GatherInstances(std::vector<anari::Instance> &instances) const;

  HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

 protected:
  virtual HdAnariMaterial::PrimvarBinding UpdateGeometry(
      HdSceneDelegate *sceneDelegate,
      HdDirtyBits *dirtyBits,
      const TfToken::Set &allPrimvars,
      const VtValue &points) = 0;
  virtual void UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
      HdInterpolation interpolation,
      const TfToken &attributeName,
      const VtValue &value) = 0;

  TfToken _GetPrimitiveBindingPoint(const TfToken &attribute);
  TfToken _GetFaceVaryingBindingPoint(const TfToken &attribute);
  TfToken _GetVertexBindingPoint(const TfToken &attribute);

  void _SetGeometryAttributeConstant(
      const TfToken &attributeName, const VtValue &v);
  void _SetGeometryAttributeArray(const TfToken &attributeName,
      const TfToken &bindingPoint,
      const VtValue &v,
      anari::DataType forcedType = ANARI_UNKNOWN);
#if USE_INSTANCE_ARRAYS
  void _SetInstanceAttributeArray(const TfToken &attributeName,
      const VtValue &v,
      anari::DataType forcedType = ANARI_UNKNOWN);
#endif

  void _InitRepr(const TfToken &reprToken, HdDirtyBits *dirtyBits) override;

#if !USE_INSTANCE_ARRAYS
  void ReleaseInstances();
#endif

  HdAnariGeometry(const HdAnariGeometry &) = delete;
  HdAnariGeometry &operator=(const HdAnariGeometry &) = delete;

 private:
  // Data //
  bool _populated{false};
  HdMeshTopology topology_;
  std::unique_ptr<HdAnariMeshUtil> meshUtil_;
  std::optional<Hd_VertexAdjacency> adjacency_;

  VtIntArray trianglePrimitiveParams_;

  HdAnariMaterial::PrimvarBinding primvarBinding_;
  std::map<TfToken, TfToken> geometryBindingPoints_;
  std::map<TfToken, TfToken> instanceBindingPoints_;

#if !USE_INSTANCE_ARRAYS
  VtMatrix4fArray transforms_UNIQUE_INSTANCES_;
  VtUIntArray ids_UNIQUE_INSTANCES_;
  std::vector<std::pair<TfToken, VtValue>> instancedPrimvar_UNIQUE_INSTANCES_;
#endif

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
