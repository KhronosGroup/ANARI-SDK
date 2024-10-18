// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "geometry.h"

#include <anari/anari.h>
#include <anari/anari_cpp/Traits.h>
#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/tf.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>
#include <stdint.h>
#include <algorithm>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <cstring>
#include <iterator>
#include <numeric>
#include <unordered_map>
#include <utility>

#include "anariTokens.h"
#include "debugCodes.h"
#include "instancer.h"
#include "renderParam.h"

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
  if (_GetVtArrayBufferData_T<VtIntArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec2iArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec3iArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec4iArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtUIntArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtFloatArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec2fArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec3fArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtVec4fArray>(v, data, size, type))
    return true;
  if (_GetVtArrayBufferData_T<VtMatrix4fArray>(v, data, size, type))
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

HdAnariGeometry::HdAnariGeometry(anari::Device d,
    const TfToken &geometryType,
    const SdfPath &id,
    const SdfPath &instancerId)
    : HdMesh(id)
{
  if (!d)
    return;

  _anari.device = d;
  _anari.geometry =
      anari::newObject<anari::Geometry>(d, geometryType.GetText());
  _anari.surface = anari::newObject<anari::Surface>(d);
  _anari.group = anari::newObject<anari::Group>(d);
#if USE_INSTANCE_ARRAYS
  _anari.instance = anari::newObject<anari::Instance>(d, "transform");
#endif

  anari::setParameter(d, _anari.surface, "geometry", _anari.geometry);
  anari::commitParameters(d, _anari.surface);

  anari::setParameterArray1D(d, _anari.group, "surface", &_anari.surface, 1);
  anari::commitParameters(d, _anari.group);

#if USE_INSTANCE_ARRAYS
  anari::setParameter(_anari.device, _anari.instance, "group", _anari.group);
  anari::commitParameters(d, _anari.instance);
#endif
}

HdAnariGeometry::~HdAnariGeometry()
{
  if (!_anari.device)
    return;
#if USE_INSTANCE_ARRAYS
  anari::release(_anari.device, _anari.instance);
#else
  ReleaseInstances();
#endif
  anari::release(_anari.device, _anari.group);
  anari::release(_anari.device, _anari.surface);
  anari::release(_anari.device, _anari.geometry);
}

HdDirtyBits HdAnariGeometry::GetInitialDirtyBitsMask() const
{
  return HdChangeTracker::AllDirty;
}

void HdAnariGeometry::Sync(HdSceneDelegate *sceneDelegate,
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

  // Handle material sync first
  SetMaterialId(sceneDelegate->GetMaterialId(id));

  HdAnariMaterial::PrimvarBinding previousBinding = primvarBinding_;
  HdAnariMaterial::PrimvarBinding updatedPrimvarBinding = previousBinding;
  if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
    HdAnariMaterial *material = static_cast<HdAnariMaterial *>(
        renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
    if (auto mat = material ? material->GetAnariMaterial() : nullptr) {
      anari::setParameter(_anari.device,
          _anari.surface,
          "material",
          material->GetAnariMaterial());
      updatedPrimvarBinding = material->GetPrimvarBinding();
    } else {
      anari::setParameter(_anari.device,
          _anari.surface,
          "material",
          renderParam->GetDefaultMaterial());
      updatedPrimvarBinding = renderParam->GetDefaultPrimvarBinding();
    }
    anari::commitParameters(_anari.device, _anari.surface);
  }

  // Enumerate primvars
  bool pointsIsComputationPrimvar = false;
  bool displayColorIsAuthored = false;
  TfToken::Set allPrimvars;
  HdExtComputationPrimvarDescriptorVector computationPrimvarDescriptors;
  for (auto i = 0; i < HdInterpolationCount; ++i) {
    auto interpolation = HdInterpolation(i);

    for (const auto &pv : sceneDelegate->GetExtComputationPrimvarDescriptors(
             id, HdInterpolation(i))) {
      allPrimvars.insert(pv.name);

      // Consider the primvar if it is dirty and is part of the new binding set
      TfToken prevBindingPoint;
      TfToken newBindingPoint;
      if (auto it = previousBinding.find(pv.name);
          it != std::cend(previousBinding))
        prevBindingPoint = it->second;
      if (auto it = updatedPrimvarBinding.find(pv.name);
          it != std::cend(updatedPrimvarBinding))
        newBindingPoint = it->second;

      if (prevBindingPoint != newBindingPoint
          || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
        computationPrimvarDescriptors.push_back(pv);
      }
      if (pv.name == HdTokens->points)
        pointsIsComputationPrimvar = true;
      if (pv.name == HdTokens->displayColor)
        displayColorIsAuthored = true;
    }
  }

  HdPrimvarDescriptorVector primvarDescriptors;
  for (auto i = 0; i < HdInterpolationCount; ++i) {
    for (const auto &pv :
        sceneDelegate->GetPrimvarDescriptors(GetId(), HdInterpolation(i))) {
      if (auto it = allPrimvars.find(pv.name); it != std::cend(allPrimvars))
        continue;

      allPrimvars.insert(pv.name);
      // Consider the primvar if it is dirty and is part of the new binding set
      TfToken prevBindingPoint;
      TfToken newBindingPoint;
      if (auto it = previousBinding.find(pv.name);
          it != std::cend(previousBinding))
        prevBindingPoint = it->second;
      if (auto it = updatedPrimvarBinding.find(pv.name);
          it != std::cend(updatedPrimvarBinding))
        newBindingPoint = it->second;

      if (prevBindingPoint != newBindingPoint
          || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
        primvarDescriptors.push_back(pv);
      }
      if (pv.name == HdTokens->displayColor)
        displayColorIsAuthored = true;
    }
  }

  // Drop all previous primvars that are not part of the new material
  // description or are bound to a different point.
  for (const auto &pvb : previousBinding) {
    if (auto it = updatedPrimvarBinding.find(pvb.first);
        it == std::cend(updatedPrimvarBinding) || it->second != pvb.second) {
      if (auto geomIt = geometryBindingPoints_.find(pvb.second);
          geomIt != std::cend(geometryBindingPoints_)) {
        anari::unsetParameter(
            _anari.device, _anari.geometry, geomIt->second.GetText());
      } else if (auto instanceIt = instanceBindingPoints_.find(pvb.second);
                 instanceIt != std::cend(instanceBindingPoints_)) {
#if USE_INSTANCE_ARRAYS
        anari::unsetParameter(
            _anari.device, _anari.instance, instanceIt->second.GetText());
#endif
      }
    }
  }

  // Assume that points are always to be bound.
  updatedPrimvarBinding.emplace(HdTokens->points, HdAnariTokens->position);

  // Gather computation primvars sources
  HdExtComputationUtils::ValueStore computationPrimvarSources =
      HdExtComputationUtils::GetComputedPrimvarValues(
          computationPrimvarDescriptors, sceneDelegate);

  // Handle displayColor
  if (auto it = updatedPrimvarBinding.find(HdTokens->displayColor);
      it != std::cend(updatedPrimvarBinding)) {
    if (!displayColorIsAuthored) {
      _SetGeometryAttributeConstant(
          it->second, VtValue(GfVec3f(0.8f, 0.8f, 0.8f)));
    } else {
      _SetGeometryAttributeConstant(it->second, VtValue());
    }
  }

  // Handle shape specific changes
  VtValue points;
  if (auto it = computationPrimvarSources.find(HdTokens->points);
      it != std::cend(computationPrimvarSources)) {
    points = it->second;
  }

  if (!points.IsHolding<VtVec3fArray>() || points.GetArraySize() == 0) {
    points = sceneDelegate->Get(id, HdTokens->points);
  }

  for (const auto &additionalPrimvar :
      UpdateGeometry(sceneDelegate, dirtyBits, allPrimvars, points)) {
    updatedPrimvarBinding.insert(additionalPrimvar);
  }

  // Primvars
  for (const auto &pvd : primvarDescriptors) {
    if (auto pvbIt = updatedPrimvarBinding.find(pvd.name);
        pvbIt != cend(updatedPrimvarBinding)) {
      const auto &value = sceneDelegate->Get(id, pvd.name);
      auto attributeName = pvbIt->second;
      UpdatePrimvarSource(
          sceneDelegate, pvd.interpolation, attributeName, value);
    }
  }

  // Computation Primvars
  for (const auto &pvd : computationPrimvarDescriptors) {
    if (auto pvbIt = updatedPrimvarBinding.find(pvd.name);
        pvbIt != cend(updatedPrimvarBinding)) {
      const auto &valueIt = computationPrimvarSources.find(pvd.name);
      if (!TF_VERIFY(valueIt != std::cend(computationPrimvarSources)))
        continue;
      const auto &value = valueIt->second;
      auto attributeName = pvbIt->second;
      UpdatePrimvarSource(
          sceneDelegate, pvd.interpolation, attributeName, value);
    }
  }

  anari::commitParameters(_anari.device, _anari.geometry);

  if (HdChangeTracker::IsDirty(HdChangeTracker::DirtyPrimID)) {
    anari::setParameter(
        _anari.device, _anari.surface, "id", uint32_t(GetPrimId()));
    anari::commitParameters(_anari.device, _anari.surface);
  }

  // Now with this instancing
// Populate instance objects.
#if !USE_INSTANCE_ARRAYS
  VtMatrix4fArray transforms_UNIQUE_INSTANCES;
  VtUIntArray ids_UNIQUE_INSTANCES;
#endif

  // Transforms //
  if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)) {
    auto baseTransform = sceneDelegate->GetTransform(id);

    // Set instance parameters
    if (GetInstancerId().IsEmpty()) {
#if USE_INSTANCE_ARRAYS
      anari::setParameter(_anari.device,
          _anari.instance,
          "transform",
          GfMatrix4f(baseTransform));
      anari::setParameter(_anari.device, _anari.instance, "id", 0u);
#else
      transforms_UNIQUE_INSTANCES.push_back(GfMatrix4f(baseTransform));
      ids_UNIQUE_INSTANCES.push_back(0);
#endif
    } else {
      auto instancer = static_cast<HdAnariInstancer *>(
          renderIndex.GetInstancer(GetInstancerId()));

      // Transforms
      const VtMatrix4dArray &transformsd =
          instancer->ComputeInstanceTransforms(id);
      VtMatrix4fArray transforms(std::size(transformsd));
      std::transform(std::cbegin(transformsd),
          std::cend(transformsd),
          std::begin(transforms),
          [&baseTransform](
              const auto &tx) { return GfMatrix4f(baseTransform * tx); });

      VtUIntArray ids(transforms.size());
      std::iota(std::begin(ids), std::end(ids), 0);

#if USE_INSTANCE_ARRAYS
      _SetInstanceAttributeArray(HdAnariTokens->transform, VtValue(transforms));
      _SetInstanceAttributeArray(HdAnariTokens->id, VtValue(ids));
#else
      transforms_UNIQUE_INSTANCES = std::move(transforms);
      ids_UNIQUE_INSTANCES = std::move(ids);
#endif
    }
  }

#if !USE_INSTANCE_ARRAYS
  std::vector<std::pair<TfToken, VtValue>> instancedPrimvar_UNIQUE_INSTANCES;
#endif

  // Primvars
  if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)) {
    auto instancer = static_cast<HdAnariInstancer *>(
        renderIndex.GetInstancer(GetInstancerId()));

    // Process primvars
    HdPrimvarDescriptorVector instancePrimvarDescriptors;
    for (auto instancerId = GetInstancerId(); !instancerId.IsEmpty();
         instancerId = renderIndex.GetInstancer(instancerId)->GetParentId()) {
      for (const auto &pv : sceneDelegate->GetPrimvarDescriptors(
               instancerId, HdInterpolationInstance)) {
        if (pv.name == HdInstancerTokens->instanceRotations
            || pv.name == HdInstancerTokens->instanceScales
            || pv.name == HdInstancerTokens->instanceTranslations
            || pv.name == HdInstancerTokens->instanceTransforms)
          continue;

        if (allPrimvars.find(pv.name) != std::cend(allPrimvars))
          continue;
        allPrimvars.insert(pv.name);

        // Consider the primvar if it is dirty and is part of the new binding
        // set
        TfToken prevBindingPoint;
        TfToken newBindingPoint;
        if (auto it = previousBinding.find(pv.name);
            it != std::cend(previousBinding))
          prevBindingPoint = it->second;
        if (auto it = updatedPrimvarBinding.find(pv.name);
            it != std::cend(updatedPrimvarBinding))
          newBindingPoint = it->second;

        auto thisInstancer = static_cast<const HdAnariInstancer *>(
            renderIndex.GetInstancer(instancerId));

        if (prevBindingPoint != newBindingPoint
            || thisInstancer->IsPrimvarDirty(pv.name)) {
          instancePrimvarDescriptors.push_back(pv);
        }
      }
    }

    for (const auto &pv : instancePrimvarDescriptors) {
      if (auto it = updatedPrimvarBinding.find(pv.name);
          it != std::cend(updatedPrimvarBinding)) {
#if USE_INSTANCE_ARRAYS
        _SetInstanceAttributeArray(
            it->second, instancer->GatherInstancePrimvar(GetId(), pv.name));
#else
        instancedPrimvar_UNIQUE_INSTANCES.emplace_back(
            it->second, instancer->GatherInstancePrimvar(GetId(), pv.name));
#endif
      }
    }
  }

#if USE_INSTANCE_ARRAYS
  anari::commitParameters(_anari.device, _anari.instance);
#else
  ReleaseInstances();
  _anari.instances.reserve(std::size(transforms_UNIQUE_INSTANCES));

  for (auto i = 0ul; i < std::size(transforms_UNIQUE_INSTANCES); ++i) {
    auto instance =
        anari::newObject<anari::Instance>(_anari.device, "transform");
    anari::setParameter(_anari.device, instance, "group", _anari.group);
    anari::setParameter(
        _anari.device, instance, "transform", transforms_UNIQUE_INSTANCES[i]);
    anari::setParameter(_anari.device, instance, "id", ids_UNIQUE_INSTANCES[i]);

    for (auto &&[attr, value] : instancedPrimvar_UNIQUE_INSTANCES) {
      if (value.IsHolding<VtFloatArray>()) {
        const auto &array = value.UncheckedGet<VtFloatArray>();
        if (i < std::size(array))
          anari::setParameter(
              _anari.device, instance, attr.GetText(), array[i]);
      } else if (value.IsHolding<VtVec2fArray>()) {
        const auto &array = value.UncheckedGet<VtVec2fArray>();
        if (i < std::size(array))
          anari::setParameter(
              _anari.device, instance, attr.GetText(), array[i]);
      } else if (value.IsHolding<VtVec3fArray>()) {
        const auto &array = value.UncheckedGet<VtVec3fArray>();
        if (i < std::size(array))
          anari::setParameter(
              _anari.device, instance, attr.GetText(), array[i]);
      } else if (value.IsHolding<VtVec4fArray>()) {
        const auto &array = value.UncheckedGet<VtVec4fArray>();
        if (i < std::size(array))
          anari::setParameter(
              _anari.device, instance, attr.GetText(), array[i]);
      }
    }
    anari::commitParameters(_anari.device, instance);
    _anari.instances.push_back(instance);
  }

  renderParam->MarkNewSceneVersion();
#endif

  primvarBinding_ = updatedPrimvarBinding;

  if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
    _UpdateVisibility(sceneDelegate, dirtyBits);
    // renderParam->MarkNewSceneVersion();
  }

  if (!_populated) {
    renderParam->RegisterGeometry(this);
    _populated = true;
  }

  *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdAnariGeometry::GatherInstances(
    std::vector<anari::Instance> &instances) const
{
  if (IsVisible()) {
#if USE_INSTANCE_ARRAYS
    instances.push_back(_anari.instance);
#else
    std::copy(std::cbegin(_anari.instances),
        std::cend(_anari.instances),
        std::back_inserter(instances));
#endif
  }
}

void HdAnariGeometry::Finalize(HdRenderParam *renderParam_)
{
  if (_populated) {
    auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
    renderParam->UnregisterGeometry(this);
    _populated = false;
  }
}

HdDirtyBits HdAnariGeometry::_PropagateDirtyBits(HdDirtyBits bits) const
{
  return bits;
}

void HdAnariGeometry::_InitRepr(
    const TfToken &reprToken, HdDirtyBits *dirtyBits)
{
  TF_UNUSED(dirtyBits);

  // Create an empty repr.
  _ReprVector::iterator it =
      std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(reprToken));
  if (it == _reprs.end()) {
    _reprs.emplace_back(reprToken, HdReprSharedPtr());
  }
}

#if !USE_INSTANCE_ARRAYS
void HdAnariGeometry::ReleaseInstances()
{
  for (const auto &instance : _anari.instances) {
    anari::release(_anari.device, instance);
  }
  _anari.instances.clear();
}
#endif

void HdAnariGeometry::_SetGeometryAttributeConstant(
    const TfToken &attributeName, const VtValue &value)
{
  auto d = _anari.device;
  auto g = _anari.geometry;

  GfVec4f attrV(0.f, 0.f, 0.f, 1.f);
  if (_GetVtValueAsAttribute(value, attrV)) {
    anari::setParameter(d, g, attributeName.GetText(), attrV);
    geometryBindingPoints_.emplace(attributeName, attributeName);
    TF_DEBUG_MSG(HD_ANARI_GEOMETRY,
        "Assigning constant %s to mesh %s\n",
        attributeName.GetText(),
        GetId().GetText());
  } else {
    if (auto it = geometryBindingPoints_.find(attributeName);
        it != std::end(geometryBindingPoints_)) {
      geometryBindingPoints_.erase(it);
      TF_DEBUG_MSG(HD_ANARI_GEOMETRY,
          "Clearing constant %s on mesh %s\n",
          attributeName.GetText(),
          GetId().GetText());
      anari::unsetParameter(d, g, attributeName.GetText());
    }
  }
}

void HdAnariGeometry::_SetGeometryAttributeArray(const TfToken &attributeName,
    const TfToken &bindingPoint,
    const VtValue &value,
    anari::DataType forcedType)
{
  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (!value.IsEmpty() && _GetVtArrayBufferData(value, &data, &size, &type)) {
    TF_DEBUG_MSG(HD_ANARI_GEOMETRY,
        "Assigning geometry primvar %s to %s on mesh %s\n",
        attributeName.GetText(),
        bindingPoint.GetText(),
        GetId().GetText());
    anari::setParameterArray1D(_anari.device,
        _anari.geometry,
        bindingPoint.GetText(),
        forcedType == ANARI_UNKNOWN ? type : forcedType,
        data,
        size);
    geometryBindingPoints_.emplace(attributeName, attributeName);
  } else {
    if (auto it = geometryBindingPoints_.find(attributeName);
        it != std::end(geometryBindingPoints_)) {
      geometryBindingPoints_.erase(it);
      anari::unsetParameter(
          _anari.device, _anari.geometry, bindingPoint.GetText());
      TF_DEBUG_MSG(HD_ANARI_GEOMETRY,
          "Clearing geometry primvar %s on %s on mesh %s\n",
          attributeName.GetText(),
          bindingPoint.GetText(),
          GetId().GetText());
    }
  }
}

#if USE_INSTANCE_ARRAYS
void HdAnariGeometry::_SetInstanceAttributeArray(const TfToken &attributeName,
    const VtValue &value,
    anari::DataType forcedType)
{
  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (!value.IsEmpty() && _GetVtArrayBufferData(value, &data, &size, &type)) {
    TF_DEBUG_MSG(HD_ANARI_GEOMETRY,
        "Assigning instance primvar %s to mesh %s\n",
        attributeName.GetText(),
        GetId().GetText());
    anari::setParameterArray1D(_anari.device,
        _anari.instance,
        attributeName.GetText(),
        forcedType == ANARI_UNKNOWN ? type : forcedType,
        data,
        size);
    instanceBindingPoints_.emplace(attributeName, attributeName);
  } else {
    if (auto it = instanceBindingPoints_.find(attributeName);
        it != std::end(instanceBindingPoints_)) {
      instanceBindingPoints_.erase(it);
      anari::unsetParameter(
          _anari.device, _anari.instance, attributeName.GetText());
      TF_DEBUG_MSG(HD_ANARI_GEOMETRY,
          "Clearing instance primvar %s on mesh %s\n",
          attributeName.GetText(),
          GetId().GetText());
    }
  }
}
#endif

TfToken HdAnariGeometry::_GetPrimitiveBindingPoint(const TfToken &attribute)
{
  if (attribute == HdAnariTokens->attribute0)
    return HdAnariTokens->primitiveAttribute0;
  if (attribute == HdAnariTokens->attribute1)
    return HdAnariTokens->primitiveAttribute1;
  if (attribute == HdAnariTokens->attribute2)
    return HdAnariTokens->primitiveAttribute2;
  if (attribute == HdAnariTokens->attribute3)
    return HdAnariTokens->primitiveAttribute3;
  if (attribute == HdAnariTokens->color)
    return HdAnariTokens->primitiveColor;

  return {};
}
TfToken HdAnariGeometry::_GetFaceVaryingBindingPoint(const TfToken &attribute)
{
  if (attribute == HdAnariTokens->attribute0)
    return HdAnariTokens->faceVaryingAttribute0;
  if (attribute == HdAnariTokens->attribute1)
    return HdAnariTokens->faceVaryingAttribute1;
  if (attribute == HdAnariTokens->attribute2)
    return HdAnariTokens->faceVaryingAttribute2;
  if (attribute == HdAnariTokens->attribute3)
    return HdAnariTokens->faceVaryingAttribute3;
  if (attribute == HdAnariTokens->color)
    return HdAnariTokens->faceVaryingColor;
  if (attribute == HdAnariTokens->normal)
    return HdAnariTokens->faceVaryingNormal;

  return {};
}
TfToken HdAnariGeometry::_GetVertexBindingPoint(const TfToken &attribute)
{
  if (attribute == HdAnariTokens->attribute0)
    return HdAnariTokens->vertexAttribute0;
  if (attribute == HdAnariTokens->attribute1)
    return HdAnariTokens->vertexAttribute1;
  if (attribute == HdAnariTokens->attribute2)
    return HdAnariTokens->vertexAttribute2;
  if (attribute == HdAnariTokens->attribute3)
    return HdAnariTokens->vertexAttribute3;
  if (attribute == HdAnariTokens->color)
    return HdAnariTokens->vertexColor;
  if (attribute == HdAnariTokens->normal)
    return HdAnariTokens->vertexNormal;
  if (attribute == HdAnariTokens->position)
    return HdAnariTokens->vertexPosition;

  return {};
}

PXR_NAMESPACE_CLOSE_SCOPE