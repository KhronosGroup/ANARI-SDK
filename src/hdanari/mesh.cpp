// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mesh.h"

#include <anari/anari.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/Traits.h>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <anari/frontend/anari_enums.h>
// pxr
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/tf.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <stdint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
// std
// #include <cstring>
#include <string>
#include <utility>

#include "debugCodes.h"
#include "instancer.h"
#include "material.h"

using namespace std::string_literals;

PXR_NAMESPACE_OPEN_SCOPE

// Helper functions ///////////////////////////////////////////////////////////

template <typename VT_ARRAY_T>
static bool _GetVtArrayBufferData_T(
    VtValue v, const void **data, size_t *size, anari::DataType *type)
{
  if (v.IsHolding<VT_ARRAY_T>()) {
    auto a = v.Get<VT_ARRAY_T>();
    *data = a.cdata();
    *size = a.size();
    *type = anari::ANARITypeFor<typename VT_ARRAY_T::value_type>::value;
    return true;
  }
  return false;
}

static bool _GetVtArrayBufferData(
    VtValue v, const void **data, size_t *size, anari::DataType *type)
{
  if (_GetVtArrayBufferData_T<VtVec2fArray>(v, data, size, type))
    return true;
  else if (_GetVtArrayBufferData_T<VtVec3fArray>(v, data, size, type))
    return true;
  return false;
}

template <typename T>
static bool _GetVtValueAsAttribute_T(VtValue v, GfVec4f &out)
{
  if (v.IsHolding<T>()) {
    auto a = v.Get<T>();
    out = GfVec4f(0.f, 0.f, 0.f, 1.f);
    std::memcpy(&out, &a, sizeof(a));
    return true;
  }
  return false;
}

template <typename T>
static bool _GetVtValueArrayAsAttribute_T(VtValue v, GfVec4f &out)
{
  if (v.IsHolding<T>()) {
    auto a = v.Get<T>();
    out = GfVec4f(0.f, 0.f, 0.f, 1.f);
    std::memcpy(&out, &a[0], sizeof(a[0]));
    return true;
  }
  return false;
}

static bool _GetVtValueAsAttribute(VtValue v, GfVec4f &out)
{
  if (_GetVtValueAsAttribute_T<float>(v, out))
    return true;
  else if (_GetVtValueAsAttribute_T<GfVec2f>(v, out))
    return true;
  else if (_GetVtValueAsAttribute_T<GfVec3f>(v, out))
    return true;
  else if (_GetVtValueAsAttribute_T<GfVec4f>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtFloatArray>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtVec2fArray>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtVec3fArray>(v, out))
    return true;
  else if (_GetVtValueArrayAsAttribute_T<VtVec4fArray>(v, out))
    return true;
  return false;
}

// HdAnariMesh definitions ////////////////////////////////////////////////////

HdAnariMesh::HdAnariMesh(
    anari::Device d, const SdfPath &id, const SdfPath &instancerId)
    : HdMesh(id)
{
  if (!d)
    return;

  _anari.device = d;
  _anari.geometry = anari::newObject<anari::Geometry>(d, "triangle");
  _anari.surface = anari::newObject<anari::Surface>(d);
  _anari.group = anari::newObject<anari::Group>(d);

  anari::setParameter(d, _anari.surface, "geometry", _anari.geometry);
  anari::commitParameters(d, _anari.surface);

  anari::setParameterArray1D(d, _anari.group, "surface", &_anari.surface, 1);
  anari::commitParameters(d, _anari.group);
}

HdAnariMesh::~HdAnariMesh()
{
  if (!_anari.device)
    return;

  _ReleaseAnariInstances();
  anari::release(_anari.device, _anari.group);
  anari::release(_anari.device, _anari.surface);
  anari::release(_anari.device, _anari.geometry);
}

HdDirtyBits HdAnariMesh::GetInitialDirtyBitsMask() const
{
  auto mask = HdChangeTracker::Clean | HdChangeTracker::InitRepr
      | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyTopology
      | HdChangeTracker::DirtyTransform | HdChangeTracker::DirtyVisibility
      | HdChangeTracker::DirtyCullStyle | HdChangeTracker::DirtyDoubleSided
      | HdChangeTracker::DirtyDisplayStyle | HdChangeTracker::DirtySubdivTags
      | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyNormals
      | HdChangeTracker::DirtyInstancer | HdChangeTracker::DirtyPrimID
      | HdChangeTracker::DirtyRepr | HdChangeTracker::DirtyMaterialId;

  return static_cast<HdDirtyBits>(mask);
}

void HdAnariMesh::Finalize(HdRenderParam *renderParam_)
{
  if (_populated) {
    auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
    renderParam->RemoveMesh(this);
    _populated = false;
  }
}

void HdAnariMesh::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam_,
    HdDirtyBits *dirtyBits,
    const TfToken & /*reprToken*/)
{
  HD_TRACE_FUNCTION();
  HF_MALLOC_TAG_FUNCTION();

  auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
  if (!renderParam || !_anari.device)
    return;

  HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
  const SdfPath &id = GetId();

  // update our own instancer data.
  _UpdateInstancer(sceneDelegate, dirtyBits);

  // Make sure we call sync on parent instancers.
  // XXX: In theory, this should be done automatically by the render index.
  // At the moment, it's done by rprim-reference.  The helper function on
  // HdInstancer needs to use a mutex to guard access, if there are actually
  // updates pending, so this might be a contention point.
  HdInstancer::_SyncInstancerAndParents(
      sceneDelegate->GetRenderIndex(), GetInstancerId());

  // Gather primvars if change either in the material or the primvars themselves
  // (on the prim or the instancer)
  bool needMaterialBindingUpdate = *dirtyBits
      & (HdChangeTracker::DirtyMaterialId | HdChangeTracker::DirtyPrimvar
          | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyNormals
          | HdChangeTracker::DirtyInstancer);

  HdPrimvarDescriptorVector primvarDescriptors;
  if (needMaterialBindingUpdate) {
    std::set<TfToken> alreadyProcessed;
    for (size_t i = 0; i < HdInterpolationCount; ++i) {
      auto interp = static_cast<HdInterpolation>(i);
      auto primvars = GetPrimvarDescriptors(sceneDelegate, interp);
      for (const HdPrimvarDescriptor &pvd : primvars) {
        if (alreadyProcessed.find(pvd.name) == cend(alreadyProcessed)) {
          alreadyProcessed.insert(pvd.name);
          primvarDescriptors.push_back(pvd);
        }
      }
    }

    for (auto instancerId = GetInstancerId(); !instancerId.IsEmpty();
         instancerId = sceneDelegate->GetRenderIndex()
                           .GetInstancer(instancerId)
                           ->GetParentId()) {
      auto primvars = sceneDelegate->GetPrimvarDescriptors(
          instancerId, HdInterpolationInstance);
      for (const HdPrimvarDescriptor &pvd : primvars) {
        if (pvd.name == HdInstancerTokens->instanceRotations
            || pvd.name == HdInstancerTokens->instanceScales
            || pvd.name == HdInstancerTokens->instanceTranslations
            || pvd.name == HdInstancerTokens->instanceTransforms)
          continue;

        if (alreadyProcessed.find(pvd.name) == cend(alreadyProcessed)) {
          alreadyProcessed.insert(pvd.name);
          primvarDescriptors.push_back(pvd);
        }
      }
    }
  }

  HdAnariMaterial::PrimvarBinding primvarBinding;

  if (*dirtyBits & HdChangeTracker::DirtyMaterialId
      || needMaterialBindingUpdate) {
    SetMaterialId(sceneDelegate->GetMaterialId(id));
    auto *material = static_cast<HdAnariMaterial *>(
        renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
    if (material) {
      auto anariMat = material->GetAnariMaterial(sceneDelegate);
      primvarBinding = material->GetPrimvarBinding();
      anari::setParameter(_anari.device, _anari.surface, "material", anariMat);
    } else {
      anari::setParameter(_anari.device,
          _anari.surface,
          "material",
          renderParam->GetANARIDefaultMaterial());
      primvarBinding = {
          {HdTokens->displayColor, TfToken("color")},
      };
    }
    anari::commitParameters(_anari.device, _anari.surface);
  }

  primvarBinding.emplace(HdTokens->points, "position");
  primvarBinding.emplace(HdTokens->normals, "normal");

  // Triangle indices //

  bool recomputePrimvars = false;
  if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
    _topology = HdMeshTopology(GetMeshTopology(sceneDelegate), 0);
    _meshUtil = std::make_unique<HdAnariMeshUtil>(&_topology, id);

    anari::unsetParameter(_anari.device, _anari.geometry, "primitive.index");

    VtVec3iArray triangulatedIndices;
    VtIntArray trianglePrimitiveParams;
    _meshUtil->ComputeTriangleIndices(
        &triangulatedIndices, &_trianglePrimitiveParams);

    if (!triangulatedIndices.empty()) {
      anari::setParameterArray1D(_anari.device,
          _anari.geometry,
          "primitive.index",
          ANARI_UINT32_VEC3,
          triangulatedIndices.cdata(),
          triangulatedIndices.size());
    }

    anari::commitParameters(_anari.device, _anari.geometry);

    anari::setParameter(
        _anari.device, _anari.surface, "id", uint32_t(GetPrimId()));
    anari::commitParameters(_anari.device, _anari.surface);

    recomputePrimvars = true;
  }

  // Primvars //

  if (recomputePrimvars
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor)
      || HdChangeTracker::IsPrimvarDirty(
          *dirtyBits, id, HdTokens->displayOpacity)) {
    _UpdatePrimvarSources(sceneDelegate,
        renderParam,
        *dirtyBits,
        primvarDescriptors,
        primvarBinding);
  }

  // Populate instance objects.

  // Transforms //

  if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)) {
    _ReleaseAnariInstances();

    auto baseTransform = GfMatrix4f(sceneDelegate->GetTransform(id));

    VtMatrix4dArray transforms;
    std::map<TfToken, VtValue> primvarMap;

    if (!GetInstancerId().IsEmpty()) {
      auto instancer = static_cast<HdAnariInstancer *>(
          renderIndex.GetInstancer(GetInstancerId()));
      transforms = instancer->ComputeInstanceTransforms(id);

      for (const auto &pvd : primvarDescriptors) {
        if (pvd.interpolation != HdInterpolationInstance)
          continue;

        if (auto it = primvarBinding.find(pvd.name);
            it != cend(primvarBinding)) {
          auto pvv = instancer->GatherInstancePrimvar(GetId(), pvd.name);
          if (!pvv.IsEmpty() && pvv.IsArrayValued() && pvv.GetArraySize() > 0) {
            primvarMap.emplace(it->second, pvv);
          }
        }
      }
    } else {
      transforms.push_back(GfMatrix4d(1.0));
    }

    _anari.instances.reserve(transforms.size());
    VtIntArray instanceIndices =
        sceneDelegate->GetInstanceIndices(GetInstancerId(), GetId());
    for (size_t i = 0; i < transforms.size(); i++) {
      const GfMatrix4d &transform = transforms[i];
      GfMatrix4f mat = baseTransform * GfMatrix4f(transform);
      auto inst = anari::newObject<anari::Instance>(_anari.device, "transform");
      anari::setParameter(_anari.device, inst, "group", _anari.group);
      anari::setParameter(_anari.device, inst, "transform", mat);
      anari::setParameter(_anari.device, inst, "id", uint32_t(i));
      for (auto &&[attributeName, vtvalue] : primvarMap) {
        union
        {
          float f;
          GfVec2f vec2;
          GfVec3f vec3;
          GfVec4f vec4;
        } value = {.vec4 = {0.0f, 0.0f, 0.0f, 1.0f}};
        HdVtBufferSource attributeBuffer(attributeName, vtvalue);

        switch (attributeBuffer.GetTupleType().type) {
        case HdTypeFloat: {
          value.f = static_cast<const float *>(attributeBuffer.GetData())[i];
          break;
        }
        case HdTypeFloatVec2: {
          value.vec2 =
              static_cast<const GfVec2f *>(attributeBuffer.GetData())[i];
          break;
        }
        case HdTypeFloatVec3: {
          value.vec3 =
              static_cast<const GfVec3f *>(attributeBuffer.GetData())[i];
          break;
        }
        case HdTypeFloatVec4: {
          value.vec4 =
              static_cast<const GfVec4f *>(attributeBuffer.GetData())[i];
          break;
        }
        default:
          continue;
        }
        anari::setParameter(
            _anari.device, inst, attributeName.GetText(), value.vec4);
      }
      anari::commitParameters(_anari.device, inst);
      _anari.instances.push_back(inst);
    }

    renderParam->MarkNewSceneVersion();
  }

  if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
    _UpdateVisibility(sceneDelegate, dirtyBits);
    renderParam->MarkNewSceneVersion();
  }

  if (!_populated) {
    renderParam->AddMesh(this);
    _populated = true;
  }

  *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdAnariMesh::AddInstances(std::vector<anari::Instance> &instances) const
{
  if (IsVisible()) {
    instances.insert(
        instances.end(), _anari.instances.begin(), _anari.instances.end());
  }
}

void HdAnariMesh::_UpdatePrimvarSources(HdSceneDelegate *sceneDelegate,
    HdAnariRenderParam *renderParams,
    HdDirtyBits dirtyBits,
    const HdPrimvarDescriptorVector &primvarDescriptors,
    HdAnariMaterial::PrimvarBinding primvarBinding)
{
  HD_TRACE_FUNCTION();
  const SdfPath &id = GetId();

  TF_VERIFY(sceneDelegate);

  for (const auto &pvd : primvarDescriptors) {
    if (auto pvbIt = primvarBinding.find(pvd.name);
        pvbIt != cend(primvarBinding)) {
      auto value = sceneDelegate->Get(id, pvd.name);
      auto bindingPoint = pvbIt->second;

      switch (pvd.interpolation) {
      case HdInterpolationConstant: {
        _SetGeometryAttributeConstant(bindingPoint, value);
        break;
      }
      case HdInterpolationUniform:
      case HdInterpolationVarying:
      case HdInterpolationVertex:
      case HdInterpolationFaceVarying: {
        _SetGeometryAttributeArray(
            bindingPoint, pvd.name, pvd.interpolation, value);
        break;
      }
      default:
        break;
      }
    }
  }

  anari::commitParameters(_anari.device, _anari.geometry);
}

void HdAnariMesh::_SetGeometryAttributeConstant(
    const TfToken &attributeName, VtValue value) const
{
  auto d = _anari.device;
  auto g = _anari.geometry;

  GfVec4f attrV(0.f, 0.f, 0.f, 1.f);
  if (_GetVtValueAsAttribute(value, attrV))
    anari::setParameter(d, g, attributeName.GetText(), attrV);
  else
    anari::unsetParameter(d, g, attributeName.GetText());
}

void HdAnariMesh::_SetGeometryAttributeArray(const TfToken &attrName,
    const TfToken &pvname,
    HdInterpolation interpolation,
    VtValue value) const
{
  static const auto bindingPrefix = std::array{
      ""s, // HdInterpolationConstant = 0,
      "primitive."s, // HdInterpolationUniform,
      "vertex."s, // HdInterpolationVarying,
      "vertex."s, // HdInterpolationVertex,
      "faceVarying."s, // HdInterpolationFaceVarying,
      ""s, // HdInterpolationInstance,
  };

  auto fullAttrName = bindingPrefix[int(interpolation)] + std::string(attrName);

  if (!value.IsArrayValued())
    return;

  auto d = _anari.device;
  auto g = _anari.geometry;
  switch (interpolation) {
  case HdInterpolationUniform: {
    VtValue perFace;
    _meshUtil->GatherPerFacePrimvar(
        GetId(), pvname, value, _trianglePrimitiveParams, &perFace);
    value = perFace;
    break;
  }
  case HdInterpolationFaceVarying: {
    HdVtBufferSource buffer(pvname, value);
    VtValue triangulatedPrimvar;
    auto success =
        _meshUtil->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

    if (success) {
      value = triangulatedPrimvar;
    } else {
      TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
      value = VtValue();
    }
    break;
  }
  default:
    break;
  }

  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (_GetVtArrayBufferData(value, &data, &size, &type)) {
    anari::setParameterArray1D(d, g, fullAttrName.c_str(), type, data, size);
  } else
    anari::unsetParameter(d, g, fullAttrName.c_str());
}

void HdAnariMesh::_ReleaseAnariInstances()
{
  for (auto inst : _anari.instances)
    anari::release(_anari.device, inst);
  _anari.instances.clear();
}

HdDirtyBits HdAnariMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
  return bits;
}

void HdAnariMesh::_InitRepr(const TfToken &reprToken, HdDirtyBits *dirtyBits)
{
  TF_UNUSED(dirtyBits);

  // Create an empty repr.
  _ReprVector::iterator it =
      std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(reprToken));
  if (it == _reprs.end()) {
    _reprs.emplace_back(reprToken, HdReprSharedPtr());
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
