// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "geometry.h"

#include "anariTokens.h"
#include "debugCodes.h"
#include "instancer.h"
#include "material.h"
#include "renderParam.h"

// anari
#include <anari/anari.h>
#include <anari/anari_cpp/Traits.h>
#include <anari/frontend/anari_enums.h>
#include <anari/frontend/type_utility.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>

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
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/pxr.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>

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

bool HdAnariGeometry::_GetVtArrayBufferData(
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

bool HdAnariGeometry::_GetVtValueAsAttribute(VtValue v, GfVec4f &out)
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
    : HdMesh(id), geometryType_(geometryType)
{
  if (!d)
    return;

  device_ = d;
}

HdAnariGeometry::~HdAnariGeometry()
{
  if (!device_)
    return;
}

HdDirtyBits HdAnariGeometry::GetInitialDirtyBitsMask() const
{
  return HdChangeTracker::AllDirty;
}

// Updates an anari geometry object.
// primvarSource stores the usable values to populate the geometry.
// updatedPrimvarsBinding stores the binding points for the primvars for this
// new sync. previousPrimvarsBinding stores the binding points for the primvars
// for the previous sync. updates stores the primvars that have been added or
// updated. removes stores the primvars that have been removed.
void HdAnariGeometry::SyncAnariGeometry(anari::Device device,
    anari::Geometry geometry,

    const PrimvarToSource &primvarSources,
    HdAnariMaterial::PrimvarBinding updatedPrimvarsBinding,
    HdAnariMaterial::PrimvarBinding previousPrimvarsBinding,

    const PrimvarStateDesc &updates,
    const PrimvarStateDesc &removes)
{
  // Traverse all removed primvars and find a possible previous binding point to
  // clear.
  for (auto primvar : removes.primvars) {
    auto bindingPointIt = previousPrimvarsBinding.find(primvar);
    if (bindingPointIt == cend(previousPrimvarsBinding))
      continue;

    auto interpolationIt = removes.primvarsInterpolation.find(primvar);
    if (interpolationIt == cend(removes.primvarsInterpolation))
      continue;

    auto bindingPoint = bindingPointIt->second;
    auto interpolation = interpolationIt->second;

    // Skip interpolation primvars as those are of interest to anari::Instance
    // only.
    if (interpolation == HdInterpolationInstance)
      continue;

    anari::unsetParameter(device,
        geometry,
        _GetBindingPoint(bindingPoint, interpolation).GetText());
  }

  // Traverse all added/updated primvars and the new binding point to set.
  for (auto primvar : updates.primvars) {
    auto primvarSourceIt = primvarSources.find(primvar);
    if (primvarSourceIt == cend(primvarSources))
      continue;

    auto bindingPointIt = updatedPrimvarsBinding.find(primvar);
    if (bindingPointIt == cend(updatedPrimvarsBinding))
      continue;

    auto interpolationIt = updates.primvarsInterpolation.find(primvar);
    if (interpolationIt == cend(updates.primvarsInterpolation))
      continue;

    auto bindingPoint = bindingPointIt->second;
    auto interpolation = interpolationIt->second;

    if (interpolation == HdInterpolationInstance)
      continue;

    switch (interpolation) {
    case HdInterpolationConstant: {
      anari::setParameter(device,
          geometry,
          bindingPoint.GetText(),
          std::get<GfVec4f>(primvarSourceIt->second));
      break;
    }
    case HdInterpolationFaceVarying:
    case HdInterpolationVarying:
    case HdInterpolationVertex:
    case HdInterpolationUniform: {
      bindingPoint = _GetBindingPoint(bindingPoint, interpolation);
      anari::setParameter(device,
          geometry,
          bindingPoint.GetText(),
          std::get<anari::Array1D>(primvarSourceIt->second));
      break;
    }
    case HdInterpolationInstance: {
      break;
    }

    case HdInterpolationCount: {
      assert(false);
    }
    }
  }
}

// Updates an anari instance object.
// primvarSource stores the usable values to populate the geometry.
// updatedPrimvarsBinding stores the binding points for the primvars for this
// new sync. previousPrimvarsBinding stores the binding points for the primvars
// for the previous sync. updates stores the primvars that have been added or
// updated. removes stores the primvars that have been removed.
void HdAnariGeometry::SyncAnariInstance(anari::Device device,
    anari::Instance instance,
    const PrimvarToSource &primvarSources,
    HdAnariMaterial::PrimvarBinding updatedPrimvarsBinding,
    HdAnariMaterial::PrimvarBinding previousPrimvarsBinding,

    const PrimvarStateDesc &updates,
    const PrimvarStateDesc &removes)
{
  // Traverse all removed primvars and find a possible previous binding point to
  // clear.
  for (auto primvar : removes.primvars) {
    auto bindingPointIt = previousPrimvarsBinding.find(primvar);
    if (bindingPointIt == cend(previousPrimvarsBinding))
      continue;

    auto interpolationIt = removes.primvarsInterpolation.find(primvar);
    if (interpolationIt == cend(removes.primvarsInterpolation))
      continue;

    auto bindingPoint = bindingPointIt->second;
    auto interpolation = interpolationIt->second;

    // Only deal with instance primvars.
    if (interpolation != HdInterpolationInstance)
      continue;

    anari::unsetParameter(device,
        instance,
        _GetBindingPoint(bindingPoint, interpolation).GetText());
  }

  // Traverse all added/updated primvars and the new binding point to set.
  for (auto primvar : updates.primvars) {
    auto primvarSourceIt = primvarSources.find(primvar);
    if (primvarSourceIt == cend(primvarSources))
      continue;

    auto bindingPointIt = updatedPrimvarsBinding.find(primvar);
    if (bindingPointIt == cend(updatedPrimvarsBinding))
      continue;

    auto interpolationIt = updates.primvarsInterpolation.find(primvar);
    if (interpolationIt == cend(updates.primvarsInterpolation))
      continue;

    auto bindingPoint = bindingPointIt->second;
    auto interpolation = interpolationIt->second;

    switch (interpolation) {
    case HdInterpolationInstance: {
      anari::setParameter(device,
          instance,
          bindingPoint.GetText(),
          std::get<anari::Array1D>(primvarSourceIt->second));
      break;
    }

    case HdInterpolationConstant:
    case HdInterpolationUniform:
    case HdInterpolationVarying:
    case HdInterpolationVertex:
    case HdInterpolationFaceVarying:
      break;

    case HdInterpolationCount: {
      assert(false);
    }
    }
  }
}

void HdAnariGeometry::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam_,
    HdDirtyBits *dirtyBits,
    const TfToken & /*reprToken*/)
{
  HD_TRACE_FUNCTION();
  HF_MALLOC_TAG_FUNCTION();

  auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
  if (!renderParam || !device_)
    return;

  HdRenderIndex &renderIndex = sceneDelegate->GetRenderIndex();
  const SdfPath &id = GetId();

  // update our own instancer data.
  _UpdateInstancer(sceneDelegate, dirtyBits);

  auto instancer = static_cast<HdAnariInstancer *>(
      renderIndex.GetInstancer(GetInstancerId()));

  // Make sure we call sync on parent instancers.
  // XXX: In theory, this should be done automatically by the render index.
  // At the moment, it's done by rprim-reference.  The helper function on
  // HdInstancer needs to use a mutex to guard access, if there are actually
  // updates pending, so this might be a contention point.
  HdInstancer::_SyncInstancerAndParents(
      sceneDelegate->GetRenderIndex(), GetInstancerId());

  // Handle material sync first
  SetMaterialId(sceneDelegate->GetMaterialId(id));

  // Enumerate primvars defiend on the geometry. Compute first, then plain.
  std::vector<TfToken> allPrimvars;
  bool pointsIsComputationPrimvar = false;
  bool displayColorIsAuthored =
      false; // Will be used to handle assigning a default value if needed.
  for (auto i = 0; i < HdInterpolationCount; ++i) {
    auto interpolation = HdInterpolation(i);

    for (const auto &pv : sceneDelegate->GetExtComputationPrimvarDescriptors(
             id, HdInterpolation(i))) {
      allPrimvars.push_back(pv.name);
      if (pv.name == HdTokens->points)
        pointsIsComputationPrimvar = true;
      if (pv.name == HdTokens->displayColor)
        displayColorIsAuthored = true;
    }
  }

  for (auto i = 0; i < HdInterpolationCount; ++i) {
    for (const auto &pv :
        sceneDelegate->GetPrimvarDescriptors(GetId(), HdInterpolation(i))) {
      allPrimvars.push_back(pv.name);
      if (pv.name == HdTokens->displayColor)
        displayColorIsAuthored = true;
    }
  }
  // Make sure this is stored we can do binary searches and set differences.
  std::sort(begin(allPrimvars), end(allPrimvars));

  // Get an exhaustive list of primvars used by the different materials
  // referenced this geometry and its subsets.
  std::vector<TfToken> activePrimvars;

  // Check for dirty primvars and primvars that are actually used
  // Handle all primvars from the main geometry and all geomsubsets.
  // Main geometry first...
  GeomSubsetInfo mainGeomInfo;

  HdAnariMaterial *material = static_cast<HdAnariMaterial *>(
      renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
  if (auto mat = material ? material->GetAnariMaterial() : nullptr) {
    mainGeomInfo.material = material->GetAnariMaterial();
    mainGeomInfo.primvarBinding = material->GetPrimvarBinding();
  } else {
    mainGeomInfo.material = renderParam->GetDefaultMaterial();
    mainGeomInfo.primvarBinding = renderParam->GetDefaultPrimvarBinding();
  }

  // Points and normals are implicit bindings
  mainGeomInfo.primvarBinding.insert(
      {HdTokens->points, HdAnariTokens->position});
  mainGeomInfo.primvarBinding.insert(
      {HdTokens->normals, HdAnariTokens->normal});

  // Same with transforms and ids for instancing
  mainGeomInfo.primvarBinding.insert(
      {HdTokens->transform, HdAnariTokens->transform});
  mainGeomInfo.primvarBinding.insert({HdTokens->primID, HdAnariTokens->id});

  for (auto primvarBinding : mainGeomInfo.primvarBinding) {
    activePrimvars.push_back(primvarBinding.first);
  }

  // ...then geomsubsets.
  GeomSubsetToInfo geomSubsetInfos;
  GeomSubsetToSpecificPrimvarBindings geomSpecificPrimvarBindings;
  for (auto subset : GetGeomSubsets(sceneDelegate, dirtyBits)) {
    HdAnariMaterial *material = static_cast<HdAnariMaterial *>(
        renderIndex.GetSprim(HdPrimTypeTokens->material, subset.materialId));
    auto geomSubsetInfo = (material && material->GetAnariMaterial())
        ? GeomSubsetInfo{material->GetAnariMaterial(),
              material->GetPrimvarBinding()}
        : GeomSubsetInfo{renderParam->GetDefaultMaterial(),
              renderParam->GetDefaultPrimvarBinding()};

    // Points and normals are implicit bindings
    geomSubsetInfo.primvarBinding.insert(
        {HdTokens->points, HdAnariTokens->position});
    geomSubsetInfo.primvarBinding.insert(
        {HdTokens->normals, HdAnariTokens->normal});

    // Same with transforms and ids for instancing
    geomSubsetInfo.primvarBinding.insert(
        {HdTokens->transform, HdAnariTokens->transform});
    geomSubsetInfo.primvarBinding.insert({HdTokens->primID, HdAnariTokens->id});

    geomSubsetInfos.insert({subset.id, geomSubsetInfo});
  }

  // Add geomsubsets primvars to the set of active primvars. This set will be
  // used to query primvars on the geometry and instancer.
  for (auto geomSubsetInfo : geomSubsetInfos) {
    for (auto primvarBinding : geomSubsetInfo.second.primvarBinding) {
      activePrimvars.push_back(primvarBinding.first);
    }
  }

  // Sort and uniqify.
  std::sort(begin(activePrimvars), end(activePrimvars));
  activePrimvars.erase(std::unique(begin(activePrimvars), end(activePrimvars)),
      end(activePrimvars));

  // Add points and normals to the set of active primvars if not there already.
  for (auto primvar : {HdTokens->points, HdTokens->normals}) {
    if (auto it = std::lower_bound(
            cbegin(activePrimvars), cend(activePrimvars), primvar);
        it != cend(activePrimvars)) {
      if (*it != primvar)
        activePrimvars.insert(it, primvar);
    } else {
      activePrimvars.push_back(primvar);
    }
  }

  // List primvars to be removed and added.
  auto previousPrimvars = std::vector<TfToken>();
  for (auto primvarArray : primvarSource_) {
    previousPrimvars.push_back(primvarArray.first);
  }
  std::sort(begin(previousPrimvars), end(previousPrimvars));

  auto removedPrimvars = std::vector<TfToken>();
  std::set_difference(cbegin(previousPrimvars),
      cend(previousPrimvars),
      cbegin(activePrimvars),
      cend(activePrimvars),
      back_inserter(removedPrimvars));

  for (auto primvar : removedPrimvars) {
    TF_DEBUG_MSG(
        HD_ANARI_RD_GEOMETRY, "Removed primvar: %s", primvar.GetText());
    auto it = primvarSource_.find(primvar);
    if (std::holds_alternative<anari::Array1D>(it->second)) {
      anari::release(device_, std::get<anari::Array1D>(it->second));
    }
    primvarSource_.erase(it);
  }

  // List new or updated primvars.
  auto updatedPrimvars = std::vector<TfToken>();
  std::set_difference(cbegin(activePrimvars),
      cend(activePrimvars),
      cbegin(removedPrimvars),
      cend(removedPrimvars),
      back_inserter(updatedPrimvars));
  auto eraseIt = std::remove_if(
      begin(updatedPrimvars), end(updatedPrimvars), [&](const auto &primvar) {
        return !HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, primvar)
            && (!instancer || !instancer->IsPrimvarDirty(primvar));
      });
  updatedPrimvars.erase(eraseIt, end(updatedPrimvars));
  assert(std::is_sorted(cbegin(updatedPrimvars), cend(updatedPrimvars)));

  // Release resources currently owned by updated primvars.
  for (auto primvar : updatedPrimvars) {
    TF_DEBUG_MSG(
        HD_ANARI_RD_GEOMETRY, "Updated primvar: %s", primvar.GetText());
    if (auto it = primvarSource_.find(primvar); it != end(primvarSource_)) {
      if (std::holds_alternative<anari::Array1D>(it->second)) {
        anari::release(device_, std::get<anari::Array1D>(it->second));
      }
      primvarSource_.erase(it);
    }
  }

  // Based on the above, gather needed primvars descriptors. To be used to
  // create primvars sources.
  // Compute primvars have precedence over regular primvars.
  auto computationPrimvarDescriptors =
      HdExtComputationPrimvarDescriptorVector();
  for (auto interpolation = 0; interpolation < HdInterpolationCount;
      ++interpolation) {
    if (interpolation == HdInterpolationInstance)
      continue;

    auto pvds = sceneDelegate->GetExtComputationPrimvarDescriptors(
        GetId(), HdInterpolation(interpolation));
    for (auto pvd : pvds) {
      if (std::binary_search(
              cbegin(updatedPrimvars), cend(updatedPrimvars), pvd.name)) {
        computationPrimvarDescriptors.push_back(pvd);
      }
    }
  }

  auto primvarDescriptors = HdPrimvarDescriptorVector();
  for (auto interpolation = 0; interpolation < HdInterpolationCount;
      ++interpolation) {
    if (interpolation == HdInterpolationInstance)
      continue;

    auto pvds = sceneDelegate->GetPrimvarDescriptors(
        GetId(), HdInterpolation(interpolation));
    for (auto pvd : pvds) {
      if (std::binary_search(
              cbegin(updatedPrimvars), cend(updatedPrimvars), pvd.name)) {
        primvarDescriptors.push_back(pvd);
      }
    }
  }

  // Get prim's compute value store.
  HdExtComputationUtils::ValueStore computationPrimvarSources =
      HdExtComputationUtils::GetComputedPrimvarValues(
          computationPrimvarDescriptors, sceneDelegate);

  // Create missing primvars. Might be needed for normals if smoothing is on
  // for a mesh for instance.
  VtVec3fArray points;
  if (auto it = computationPrimvarSources.find(HdTokens->points);
      it != std::cend(computationPrimvarSources)) {
    if (it->second.IsHolding<VtVec3fArray>())
      points = it->second.UncheckedGet<VtVec3fArray>();
  }

  if (std::size(points) == 0) {
    if (auto vtPoints = sceneDelegate->Get(id, HdTokens->points);
        vtPoints.IsHolding<VtVec3fArray>())
      points = vtPoints.UncheckedGet<VtVec3fArray>();
  }

  // Make sure we have all the information to do the actual parameter binding.
  std::unordered_map<TfToken, HdInterpolation, TfToken::HashFunctor>
      primvarInterpolation;

  // Create resources
  {
    for (auto pvd : computationPrimvarDescriptors) {
      const auto &valueIt = computationPrimvarSources.find(pvd.name);
      if (!TF_VERIFY(valueIt != std::cend(computationPrimvarSources)))
        continue;

      // FIXME: What if an instance prim is a computation primvar. This cannot
      // work with the actual implementation of GatherInstancePrimvar.
      auto source = UpdatePrimvarSource(
          sceneDelegate, pvd.interpolation, pvd.name, valueIt->second);

      // The spot should always be free as it was removed if needed when
      // processing primvar removal.
      primvarSource_.insert({pvd.name, source});
      primvarInterpolation.insert({pvd.name, pvd.interpolation});
    }

    for (auto pvd : primvarDescriptors) {
      if (primvarSource_.count(pvd.name) != 0) {
        continue;
      }
      auto source = UpdatePrimvarSource(sceneDelegate,
          pvd.interpolation,
          pvd.name,
          sceneDelegate->Get(id, pvd.name));

      primvarSource_.insert({pvd.name, source});
      primvarInterpolation.insert({pvd.name, pvd.interpolation});
    }

    if (instancer) {
      // Kind of a special handling here. Given that we don't have an explicit
      // list of instance primvars, we need to gather them here, based on
      // updated primvars
      for (auto primvar : updatedPrimvars) {
        // If a geometry based primvar exists, let it take precedence over the
        // instance primvar.
        if (primvarSource_.count(primvar) != 0) {
          continue;
        }

        auto value = instancer->GatherInstancePrimvar(GetId(), primvar);
        if (value.IsArrayValued()) {
          primvarSource_.insert({primvar, _GetAttributeArray(value)});
          primvarInterpolation.insert({primvar, HdInterpolationInstance});
        }
      }
    }
  }

  // Create instance related variables
  if (instancer) {
    // FIXME: Those transformed are shared between all prims under the same
    // instancer hierarchy and yet we recompute those per prim. This should be
    // cached at the instancer level and returned as anari::Array1D.
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)
        || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)
        || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)) {
      auto baseTransform = sceneDelegate->GetTransform(id);

      // Transforms
      const VtMatrix4dArray &transformsd =
          instancer->ComputeInstanceTransforms(id);
      VtMatrix4fArray transforms(std::size(transformsd));
      std::transform(std::cbegin(transformsd),
          std::cend(transformsd),
          std::begin(transforms),
          [&baseTransform](
              const auto &tx) { return GfMatrix4f(baseTransform * tx); });

      auto transformsArray =
          anari::newArray1D(device_, ANARI_FLOAT32_MAT4, std::size(transforms));
      {
        auto ptr = anari::map<void>(device_, transformsArray);
        std::memcpy(ptr,
            std::data(transforms),
            std::size(transforms) * anari::sizeOf(ANARI_FLOAT32_MAT4));
        anari::unmap(device_, transformsArray);
      }

      auto transformsArrayIt = primvarSource_.find(HdTokens->transform);
      if (transformsArrayIt == end(primvarSource_)) {
        primvarSource_.insert(
            transformsArrayIt, {HdTokens->transform, transformsArray});
      } else {
        anari::release(
            device_, std::get<anari::Array1D>(transformsArrayIt->second));
        transformsArrayIt->second = transformsArray;
      }
      updatedPrimvars.push_back(HdTokens->transform);
      primvarInterpolation.insert(
          {HdTokens->transform, HdInterpolationInstance});

      VtUIntArray ids(transforms.size());
      std::iota(std::begin(ids), std::end(ids), 0);

      auto idsArray = anari::newArray1D(device_, ANARI_UINT32, std::size(ids));
      {
        auto ptr = anari::map<void>(device_, idsArray);
        std::memcpy(
            ptr, std::data(ids), std::size(ids) * anari::sizeOf(ANARI_UINT32));
        anari::unmap(device_, idsArray);
      }
      auto idsArrayIt = primvarSource_.find(HdTokens->primID);
      if (idsArrayIt == end(primvarSource_)) {
        primvarSource_.insert(idsArrayIt, {HdTokens->primID, idsArray});
      } else {
        anari::release(device_, std::get<anari::Array1D>(idsArrayIt->second));
        idsArrayIt->second = idsArray;
      }
      updatedPrimvars.push_back(HdTokens->primID);
      primvarInterpolation.insert({HdTokens->primID, HdInterpolationInstance});
    }
  }

  // Handle anari objects creation
  std::vector<anari::Instance> instances;
  if (geomSubsetInfos.empty()) {
    //  Use main geom is no subset is defined.
    geomSubsetInfos.insert({GetId(), mainGeomInfo});
  }

  // Release geomsubset that are not used anymore.
  for (auto it = geomSubsetGeometry_.begin();
      it != geomSubsetGeometry_.end();) {
    if (geomSubsetInfos.count(it->first) == 0) {
      anari::release(device_, it->second.geometry);
      anari::release(device_, it->second.surface);
      anari::release(device_, it->second.group);
      anari::release(device_, it->second.instance);
      it = geomSubsetGeometry_.erase(it);
    } else {
      ++it;
    }
  }

  // Traverse all the subset (including prim's main geometry) and build related
  // anari objects.
  for (auto &&[subsetId, geomInfo] : geomSubsetInfos) {
    auto &geometryDesc = geomSubsetGeometry_[subsetId];

    PrimvarStateDesc removedState;
    PrimvarStateDesc updatedState;
    HdAnariMaterial::PrimvarBinding newPrimvarsBinding =
        geomInfo.primvarBinding;
    HdAnariMaterial::PrimvarBinding previousPrimvarsBinding = {};

    if (!geometryDesc.instance) {
      geometryDesc.geometry =
          anari::newObject<anari::Geometry>(device_, geometryType_.GetText());

      geometryDesc.surface = anari::newObject<anari::Surface>(device_);
      anari::setParameter(
          device_, geometryDesc.surface, "geometry", geometryDesc.geometry);
      anari::setParameter(
          device_, geometryDesc.surface, "id", uint32_t(GetPrimId()));
      anari::setParameter(
          device_, geometryDesc.surface, "material", geomInfo.material);
      anari::commitParameters(device_, geometryDesc.surface);

      geometryDesc.group = anari::newObject<anari::Group>(device_);
      std::array<anari::Surface, 1> surfaces{geometryDesc.surface};
      anari::setParameterArray1D(device_,
          geometryDesc.group,
          "surface",
          data(surfaces),
          size(surfaces));
      anari::commitParameters(device_, geometryDesc.group);

      geometryDesc.instance =
          anari::newObject<anari::Instance>(device_, "transform");
      auto baseTransform = sceneDelegate->GetTransform(GetId());
      anari::setParameter(device_,
          geometryDesc.instance,
          "transform",
          GfMatrix4f(baseTransform));
      anari::setParameter(device_, geometryDesc.instance, "id", 0u);
      anari::setParameter(
          device_, geometryDesc.instance, "group", geometryDesc.group);
      anari::commitParameters(device_, geometryDesc.instance);

      // First time, everything is new
      updatedState = {
          activePrimvars,
          primvarInterpolation,
      };
      newPrimvarsBinding = geomInfo.primvarBinding;
    } else {
      // Update
      removedState = {
          removedPrimvars,
          primvarInterpolation_,
      };
      updatedState = {
          updatedPrimvars,
          primvarInterpolation,
      };

      previousPrimvarsBinding = mainGeomInfo_.primvarBinding;
      newPrimvarsBinding = mainGeomInfo.primvarBinding;
    }

    // FIXME: Do the same with displayOpacity and maybe others...
    if (displayColorIsAuthored) {
      // Make sure any previous default display color is removed so the actual
      // authored one is used.
      if (std::binary_search(cbegin(updatedPrimvars),
              cend(updatedPrimvars),
              HdTokens->displayColor)) {
        // Clear the constant value so any other can be used instead
        if (auto it = previousPrimvarsBinding.find(HdTokens->displayColor);
            it != cend(previousPrimvarsBinding)) {
          anari::unsetParameter(
              device_, geometryDesc.geometry, it->second.GetText());
        }
      }
    } else {
      // If the material depends on displayColor, make sure we handle the case
      // it is not authored.
      if (auto it = newPrimvarsBinding.find(HdTokens->displayColor);
          it != cend(newPrimvarsBinding)) {
        anari::setParameter(device_,
            geometryDesc.geometry,
            it->second.GetText(),
            GfVec4f(0.8f, 0.8f, 0.8f, 1.0f));
      }
    }
    // Apply the changes to the geometry object
    SyncAnariGeometry(device_,
        geometryDesc.geometry,
        primvarSource_,
        newPrimvarsBinding,
        previousPrimvarsBinding,

        updatedState,
        removedState);

    // We do try and get them at each sync, as it is not clear when those are
    // dirtied. The expectation is that the implementation of
    // GetGeomSpecificPrimvars is doing the caching.
    auto geomSpecificBindingPoints = GetGeomSpecificPrimvars(sceneDelegate,
        dirtyBits,
        TfToken::Set(cbegin(allPrimvars), cend(allPrimvars)),
        points,
        subsetId);

    for (auto &&[bindingPoint, array] : geomSpecificBindingPoints) {
      geomSpecificPrimvarBindings[subsetId].push_back(bindingPoint);
      anari::setParameter(
          device_, geometryDesc.geometry, bindingPoint.GetText(), array);
    }

    // Clear geomspecfic binding points set by previous sync that are no more
    // used.
    std::vector<TfToken> geomSpecificRemovedBindings;
    std::set_difference(cbegin(geomSpecificPrimvarBindings_[subsetId]),
        cend(geomSpecificPrimvarBindings_[subsetId]),
        cbegin(geomSpecificPrimvarBindings[subsetId]),
        cend(geomSpecificPrimvarBindings[subsetId]),
        back_inserter(geomSpecificRemovedBindings));

    for (const auto &bindingPoint : geomSpecificRemovedBindings) {
      anari::unsetParameter(
          device_, geometryDesc.geometry, bindingPoint.GetText());
    }

    anari::commitParameters(device_, geometryDesc.geometry);

    // Material update
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
      anari::setParameter(
          device_, geometryDesc.surface, "material", geomInfo.material);
    }
    anari::commitParameters(device_, geometryDesc.surface);

    // Instance update
    if (instancer) {
      SyncAnariInstance(device_,
          geometryDesc.instance,
          primvarSource_,
          newPrimvarsBinding,
          previousPrimvarsBinding,

          updatedState,
          removedState);
    } else {
      auto baseTransform = sceneDelegate->GetTransform(GetId());
      anari::setParameter(device_,
          geometryDesc.instance,
          "transform",
          GfMatrix4f(baseTransform));
      anari::setParameter(device_, geometryDesc.instance, "id", 0u);
    }

    anari::commitParameters(device_, geometryDesc.instance);
    instances.push_back(geometryDesc.instance);
  }

  if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
    _UpdateVisibility(sceneDelegate, dirtyBits);
    renderParam->MarkNewSceneVersion();
  }

  if (!_populated) {
    renderParam->RegisterGeometry(this);
    _populated = true;
  }

  instances_ = instances;

  geomSubsetInfo_ = std::move(geomSubsetInfos);
  primvarInterpolation_ = std::move(primvarInterpolation);
  geomSpecificPrimvarBindings_ = std::move(geomSpecificPrimvarBindings);

  *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void HdAnariGeometry::GatherInstances(
    std::vector<anari::Instance> &instances) const
{
  if (IsVisible()) {
    std::copy(cbegin(instances_), cend(instances_), back_inserter(instances));
  }
}

void HdAnariGeometry::Finalize(HdRenderParam *renderParam_)
{
  if (_populated) {
    auto *renderParam = static_cast<HdAnariRenderParam *>(renderParam_);
    renderParam->UnregisterGeometry(this);
    _populated = false;
  }

  // Release geometry
  for (auto &&[_, geometryDesc] : geomSubsetGeometry_) {
    anari::release(device_, geometryDesc.geometry);
    anari::release(device_, geometryDesc.surface);
    anari::release(device_, geometryDesc.group);
    anari::release(device_, geometryDesc.instance);
  }

  // Release primvar sources
  for (const auto &primvarSource : primvarSource_) {
    if (std::holds_alternative<anari::Array1D>(primvarSource.second)) {
      anari::release(device_, std::get<anari::Array1D>(primvarSource.second));
    }
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

anari::Array1D HdAnariGeometry::_GetAttributeArray(
    const VtValue &value, anari::DataType overrideType)
{
  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (!value.IsEmpty() && _GetVtArrayBufferData(value, &data, &size, &type)
      && size) {
    // Don't assume any ownership of the data. Map can copy asap.
    if (overrideType != ANARI_UNKNOWN)
      type = overrideType;
    auto array = anari::newArray1D(device_, type, size);
    auto ptr = anari::map<void>(device_, array);
    std::memcpy(ptr, data, size * anari::sizeOf(type));
    anari::unmap(device_, array);
    return array;
  }

  TF_RUNTIME_ERROR("Cannot extract value buffer from VtValue");
  return {};
}

TfToken HdAnariGeometry::_GetBindingPoint(
    const TfToken &token, HdInterpolation interpolation)
{
  switch (interpolation) {
  case HdInterpolationConstant: {
    return token;
  }
  case HdInterpolationUniform: {
    return _GetPrimitiveBindingPoint(token);
  }
  case HdInterpolationVarying:
  case HdInterpolationVertex: {
    return _GetVertexBindingPoint(token);
  }
  case HdInterpolationFaceVarying: {
    return _GetFaceVaryingBindingPoint(token);
  }
  case HdInterpolationInstance: {
    return token;
  }
  case HdInterpolationCount:
    assert(false);
  }

  return {};
}

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