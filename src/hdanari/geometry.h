// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "material.h"

// anari
#include <anari/frontend/anari_enums.h>
#include <anari/anari_cpp.hpp>
// pxr
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/geomSubset.h>
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
// std
#include <cstddef>
#include <unordered_map>
#include <variant>
#include <vector>

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
  // Used to handle shape specific (as in non material) primvars.
  // For instance normals for meshs or radius for points.
  struct GeomSpecificPrimVar
  {
    TfToken bindingPoint;
    anari::Array1D array;
  };
  using GeomSpecificPrimvars = std::vector<GeomSpecificPrimVar>;

  virtual GeomSpecificPrimvars GetGeomSpecificPrimvars(
      HdSceneDelegate *sceneDelegate,
      HdDirtyBits *dirtyBits,
      const TfToken::Set &allPrimvars,
      const VtVec3fArray &points,
      const SdfPath &geomsetId = {})
  {
    return {};
  };

  // Store the actual value of a primvar source.
  using PrimvarSource = std::variant<std::monostate, GfVec4f, anari::Array1D>;
  // Shape specific creation of a primvar source based on its value and
  // interpolation. This is used to create the actual constant or
  // anari::Array1D.
  virtual PrimvarSource UpdatePrimvarSource(HdSceneDelegate *sceneDelegate,
      HdInterpolation interpolation,
      const TfToken &attributeName,
      const VtValue &value) = 0;

  // Helper functions returning anari attribute name based on the given
  // HdInterpolation.
  static TfToken _GetBindingPoint(
      const TfToken &token, HdInterpolation interpolation);

  static TfToken _GetPrimitiveBindingPoint(const TfToken &attribute);
  static TfToken _GetFaceVaryingBindingPoint(const TfToken &attribute);
  static TfToken _GetVertexBindingPoint(const TfToken &attribute);

  // Creates an array out of the given VtValue.
  anari::Array1D _GetAttributeArray(
      const VtValue &v, anari::DataType forcedType = ANARI_UNKNOWN);

  void _InitRepr(const TfToken &reprToken, HdDirtyBits *dirtyBits) override;

  // More helper  function to extract data from VtValue.
  static bool _GetVtValueAsAttribute(VtValue v, GfVec4f &out);
  static bool _GetVtArrayBufferData(
      VtValue v, const void **data, size_t *size, anari::DataType *type);

  HdAnariGeometry(const HdAnariGeometry &) = delete;
  HdAnariGeometry &operator=(const HdAnariGeometry &) = delete;

  // Returns the shape geomsubsets.
  virtual HdGeomSubsets GetGeomSubsets(
      HdSceneDelegate *sceneDelegate, HdDirtyBits *dirtyBits)
  {
    return {};
  }

 private:
  // Data //
  bool _populated{false};
  // The anari geometry type.
  TfToken geometryType_;

  // Stores material and related primvar binndings for a given geomsubset.
  struct GeomSubsetInfo
  {
    anari::Material material;
    HdAnariMaterial::PrimvarBinding primvarBinding;
  };
  // main and geomsubset infos.
  GeomSubsetInfo mainGeomInfo_;
  using GeomSubsetToInfo =
      std::unordered_map<SdfPath, GeomSubsetInfo, SdfPath::Hash>;
  GeomSubsetToInfo geomSubsetInfo_;

  // Primvar source cache
  using PrimvarToSource =
      std::unordered_map<TfToken, PrimvarSource, TfToken::HashFunctor>;
  using PrimvarToInterpolation =
      std::unordered_map<TfToken, HdInterpolation, TfToken::HashFunctor>;

  // Keep track of which primvars are known, their bindings and interpolation
  PrimvarToSource primvarSource_;
  PrimvarToInterpolation primvarInterpolation_;
  HdAnariMaterial::PrimvarBinding primvarBindings_;

  // Same with geometry specific primvars
  using GeomSpecificPrimvarBindings = TfTokenVector;
  using GeomSubsetToSpecificPrimvarBindings =
      std::unordered_map<SdfPath, GeomSpecificPrimvarBindings, SdfPath::Hash>;
  GeomSubsetToSpecificPrimvarBindings geomSpecificPrimvarBindings_;

  // Anari objects for a geomsubset
  struct AnariInstanceDesc
  {
    anari::Geometry geometry{};
    anari::Surface surface{};
    anari::Group group{};
    anari::Instance instance{};
  };

  using GeomSubsetToAnariInstanceDesc =
      std::unordered_map<SdfPath, AnariInstanceDesc, SdfPath::Hash>;
  GeomSubsetToAnariInstanceDesc geomSubsetGeometry_;

 protected:
  anari::Device device_{nullptr};
  std::vector<anari::Instance> instances_;

  // Stores the state of primvars and their interpolation.
  // Used to handle removal and additon of primvars compared to previous sync.
  struct PrimvarStateDesc
  {
    std::vector<TfToken> primvars;
    std::unordered_map<TfToken, HdInterpolation, TfToken::HashFunctor>
        primvarsInterpolation;
  };

  // Handles updating a single anari goemetry.
  static void SyncAnariGeometry(anari::Device device,
      anari::Geometry geometry,

      const PrimvarToSource &primvarSources,
      HdAnariMaterial::PrimvarBinding updatedPrimvarsBinding,
      HdAnariMaterial::PrimvarBinding previousPrimvarsBinding,

      const PrimvarStateDesc &updates,
      const PrimvarStateDesc &removes);

  // Handles updating a single anari instance.
  static void SyncAnariInstance(anari::Device device,
      anari::Instance instance,

      const PrimvarToSource &primvarSources,
      HdAnariMaterial::PrimvarBinding updatedPrimvarsBinding,
      HdAnariMaterial::PrimvarBinding previousPrimvarsBinding,

      const PrimvarStateDesc &updates,
      const PrimvarStateDesc &removes);
};

PXR_NAMESPACE_CLOSE_SCOPE
