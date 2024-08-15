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
#include <pxr/base/tf/diagnosticLite.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/tf.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/base/vt/types.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/extComputation.h>
#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/smoothNormals.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <stdint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <set>
// std
// #include <cstring>
#include <string>
#include <utility>

#include "anariTokens.h"
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
  if (_GetVtArrayBufferData_T<VtIntArray>(v, data, size, type))
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

// Helpers data
static const auto primitiveBindingPoints = std::map<const TfToken, const TfToken>{
    {HdAnariTokens->attribute0, HdAnariTokens->primitiveAttribute0},
    {HdAnariTokens->attribute1, HdAnariTokens->primitiveAttribute1},
    {HdAnariTokens->attribute2, HdAnariTokens->primitiveAttribute2},
    {HdAnariTokens->attribute3, HdAnariTokens->primitiveAttribute3},
    {HdAnariTokens->color, HdAnariTokens->primitiveColor},
};

static const auto vertexBindingPoints = std::map<const TfToken, const TfToken>{
    {HdAnariTokens->attribute0, HdAnariTokens->vertexAttribute0},
    {HdAnariTokens->attribute1, HdAnariTokens->vertexAttribute1},
    {HdAnariTokens->attribute2, HdAnariTokens->vertexAttribute2},
    {HdAnariTokens->attribute3, HdAnariTokens->vertexAttribute3},
    {HdAnariTokens->color, HdAnariTokens->vertexColor},
    {HdAnariTokens->normal, HdAnariTokens->vertexNormal},
    {HdAnariTokens->position, HdAnariTokens->vertexPosition},
};

static const auto faceVaryingBindingPoints = std::map<const TfToken, const TfToken>{
    {HdAnariTokens->attribute0, HdAnariTokens->faceVaryingAttribute0},
    {HdAnariTokens->attribute1, HdAnariTokens->faceVaryingAttribute1},
    {HdAnariTokens->attribute2, HdAnariTokens->faceVaryingAttribute2},
    {HdAnariTokens->attribute3, HdAnariTokens->faceVaryingAttribute3},
    {HdAnariTokens->color, HdAnariTokens->faceVaryingColor},
    {HdAnariTokens->normal, HdAnariTokens->faceVaryingNormal},
};

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
  _anari.instance = anari::newObject<anari::Instance>(d, "transform");

  anari::setParameter(d, _anari.surface, "geometry", _anari.geometry);
  anari::commitParameters(d, _anari.surface);

  anari::setParameterArray1D(d, _anari.group, "surface", &_anari.surface, 1);
  anari::commitParameters(d, _anari.group);

  anari::setParameter(_anari.device, _anari.instance, "group", _anari.group);
  anari::commitParameters(d, _anari.instance);
}

HdAnariMesh::~HdAnariMesh()
{
  if (!_anari.device)
    return;

  anari::release(_anari.device, _anari.instance);
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

  // Handle material sync first
  SetMaterialId(sceneDelegate->GetMaterialId(id));

  HdAnariMaterial::PrimvarBinding previousBinding = primvarBinding_;
  HdAnariMaterial::PrimvarBinding updatedPrimvarBinding = previousBinding;
  if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
    HdAnariMaterial* material = static_cast<HdAnariMaterial *>(renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
    if (auto mat = material ? material->GetAnariMaterial() : nullptr) {
      anari::setParameter(_anari.device, _anari.surface, "material", material->GetAnariMaterial());
      updatedPrimvarBinding = material->GetPrimvarBinding();
    } else {
      anari::setParameter(_anari.device, _anari.surface, "material", renderParam->GetDefaultMaterial());
      updatedPrimvarBinding = renderParam->GetDefaultPrimvarBinding();
    }
    anari::commitParameters(_anari.device, _anari.surface);
  }

  // Enumerate primvars
  bool pointsIsComputationPrimvar = false;
  bool normalIsAuthored = false;
  bool displayColorIsAuthored = false;
  TfToken::Set allPrimvars;
  HdExtComputationPrimvarDescriptorVector computationPrimvarDescriptors;
  for (auto i = 0; i < HdInterpolationCount; ++i) {
    auto interpolation = HdInterpolation(i);
    
    for (const auto& pv : sceneDelegate->GetExtComputationPrimvarDescriptors(id, HdInterpolation(i))) {
      allPrimvars.insert(pv.name);

      // Consider the primvar if it is dirty and is part of the new binding set
      TfToken prevBindingPoint;
      TfToken newBindingPoint;
      if (auto it = previousBinding.find(pv.name); it != std::cend(previousBinding)) prevBindingPoint = it->second;
      if (auto it = updatedPrimvarBinding.find(pv.name); it != std::cend(updatedPrimvarBinding)) newBindingPoint = it->second;
      
      if (prevBindingPoint != newBindingPoint || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
          computationPrimvarDescriptors.push_back(pv);
      }
      if (pv.name == HdTokens->points) pointsIsComputationPrimvar = true;
      if (pv.name == HdTokens->displayColor) displayColorIsAuthored = true;
      if (pv.name == HdTokens->normals) normalIsAuthored = true;
    }
  }

  HdPrimvarDescriptorVector primvarDescriptors;
  for (auto i = 0; i < HdInterpolationCount; ++i) {
    for (const auto& pv : sceneDelegate->GetPrimvarDescriptors(GetId(), HdInterpolation(i))) {
      if (auto it = allPrimvars.find(pv.name); it != std::cend(allPrimvars)) continue;

      allPrimvars.insert(pv.name);
      // Consider the primvar if it is dirty and is part of the new binding set
      TfToken prevBindingPoint;
      TfToken newBindingPoint;
      if (auto it = previousBinding.find(pv.name); it != std::cend(previousBinding)) prevBindingPoint = it->second;
      if (auto it = updatedPrimvarBinding.find(pv.name); it != std::cend(updatedPrimvarBinding)) newBindingPoint = it->second;
      
      if (prevBindingPoint != newBindingPoint || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
          primvarDescriptors.push_back(pv);
      }
      if (pv.name == HdTokens->displayColor) displayColorIsAuthored = true;
      if (pv.name == HdTokens->normals) normalIsAuthored = true;
    }
  }

  // Drop all previous primvars that are not part of the new material description or are bound to a different point.
  for (const auto& pvb : previousBinding) {
    if (auto it = updatedPrimvarBinding.find(pvb.first);
        it == std::cend(updatedPrimvarBinding) || it->second != pvb.second) {
          if (auto geomIt = geometryBindingPoints_.find(pvb.second); geomIt != std::cend(geometryBindingPoints_)) {
            anari::unsetParameter(_anari.device, _anari.geometry, geomIt->second.GetText());
          } else if (auto instanceIt = instanceBindingPoints_.find(pvb.second); instanceIt != std::cend(instanceBindingPoints_)) {
            anari::unsetParameter(_anari.device, _anari.instance, instanceIt->second.GetText());
          }
        }
  }

  // Assume that points are always to be bound.
  updatedPrimvarBinding.emplace(HdTokens->points, HdAnariTokens->position);

  // Gather computation primvars sources
  HdExtComputationUtils::ValueStore computationPrimvarSources =
      HdExtComputationUtils::GetComputedPrimvarValues(computationPrimvarDescriptors, sceneDelegate);

  // Triangle indices //
  if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
    topology_ = HdMeshTopology(GetMeshTopology(sceneDelegate), 0);
    meshUtil_ = std::make_unique<HdAnariMeshUtil>(&topology_, id);
    adjacency_.reset();

    anari::unsetParameter(_anari.device, _anari.geometry, HdAnariTokens->primitiveIndex.GetText());

    VtVec3iArray triangulatedIndices;
    VtIntArray trianglePrimitiveParams;
    meshUtil_->ComputeTriangleIndices(
        &triangulatedIndices, &trianglePrimitiveParams_);

    if (!triangulatedIndices.empty()) {
      anari::setParameterArray1D(_anari.device,
          _anari.geometry,
          HdAnariTokens->primitiveIndex.GetText(),
          ANARI_UINT32_VEC3,
          triangulatedIndices.cdata(),
          triangulatedIndices.size());
    }

    anari::commitParameters(_anari.device, _anari.geometry);

    anari::setParameter(
        _anari.device, _anari.surface, "id", uint32_t(GetPrimId()));
    anari::commitParameters(_anari.device, _anari.surface);
  }

  // Handle displayColor
  if (auto it = updatedPrimvarBinding.find(HdTokens->displayColor); it != std::cend(updatedPrimvarBinding)) {
    if (!displayColorIsAuthored) {
      _SetGeometryAttributeConstant(it->second,VtValue(GfVec3f(0.8f, 0.8f, 0.8f)));
    } else {
      _SetGeometryAttributeConstant(it->second,VtValue());
    }
  }

  // Handle normals
  bool doSmoothNormals = (topology_.GetScheme() != PxOsdOpenSubdivTokens->none) && (topology_.GetScheme() != PxOsdOpenSubdivTokens->bilinear);
  if (normalIsAuthored) updatedPrimvarBinding.emplace(HdTokens->normals, HdAnariTokens->normal);

  if (!normalIsAuthored && doSmoothNormals) {
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
      if (!adjacency_.has_value()) {
        adjacency_.emplace();
        adjacency_->BuildAdjacencyTable(&topology_);
      }
      VtValue pointsVt;
      if (pointsIsComputationPrimvar) {
        auto it = computationPrimvarSources.find(HdTokens->points);
        pointsVt = it->second;
      }
      
      if (!pointsVt.IsHolding<VtVec3fArray>() || pointsVt.GetArraySize() == 0) {
        pointsVt = sceneDelegate->Get(id, HdTokens->points);
      }

      if (TF_VERIFY(pointsVt.IsHolding<VtVec3fArray>() && pointsVt.GetArraySize() > 0)) {
        const auto& points = pointsVt.Get<VtVec3fArray>();
        const VtVec3fArray& normals = Hd_SmoothNormals::ComputeSmoothNormals(
        &*adjacency_, std::size(points), std::cbegin(points));

        _SetGeometryAttributeArray(HdAnariTokens->normal, HdAnariTokens->vertexNormal, VtValue(normals));
      } else {
        TF_RUNTIME_ERROR("Cannot find a valid points source to compute normal for %s\n", id.GetText());
        _SetGeometryAttributeArray(HdAnariTokens->normal, HdAnariTokens->vertexNormal, VtValue());
      }      
    }
  } else if (geometryBindingPoints_.find(HdAnariTokens->normal) != std::cend(geometryBindingPoints_)) {
    _SetGeometryAttributeArray(HdAnariTokens->normal, HdAnariTokens->vertexNormal, VtValue());
  }

  _UpdatePrimvarSources(sceneDelegate, primvarDescriptors, updatedPrimvarBinding);
  _UpdateComputationPrimvarSources(sceneDelegate, computationPrimvarDescriptors, computationPrimvarSources, updatedPrimvarBinding);

  anari::commitParameters(_anari.device, _anari.geometry);

  // Populate instance objects.

  // Transforms //
  if (HdChangeTracker::IsTransformDirty(*dirtyBits, id) || HdChangeTracker::IsInstancerDirty(*dirtyBits, id) || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)) {

    auto baseTransform = sceneDelegate->GetTransform(id);

    // Set instance parameters
    if (GetInstancerId().IsEmpty()) {
      anari::setParameter(_anari.device, _anari.instance, "transform", GfMatrix4f(baseTransform));
      anari::setParameter(_anari.device, _anari.instance, "id", 0);
    } else {
      auto instancer = static_cast<HdAnariInstancer *>(renderIndex.GetInstancer(GetInstancerId()));

      // Transforms
      const VtMatrix4dArray& transformsd = instancer->ComputeInstanceTransforms(id);
      VtMatrix4fArray transforms(std::size(transformsd));
      std::transform(std::cbegin(transformsd), std::cend(transformsd), std::begin(transforms), [&baseTransform](const auto& tx) {
        return GfMatrix4f(baseTransform * tx);
      });

      VtUIntArray ids(transforms.size());
      std::iota(std::begin(ids), std::end(ids), 0);

      _SetInstanceAttributeArray(HdAnariTokens->transform, VtValue(transforms));
      _SetInstanceAttributeArray(HdAnariTokens->id, VtValue(ids));
    }
  }

  // Primvars
  if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id) || HdChangeTracker::IsInstancerDirty(*dirtyBits, id) || HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id))
  {
      auto instancer = static_cast<HdAnariInstancer *>(renderIndex.GetInstancer(GetInstancerId()));

      // Process primvars
      HdPrimvarDescriptorVector instancePrimvarDescriptors;
      for (auto instancerId = GetInstancerId(); !instancerId.IsEmpty(); instancerId = renderIndex.GetInstancer(instancerId)->GetParentId()) {
        for (const auto& pv : sceneDelegate->GetPrimvarDescriptors(instancerId, HdInterpolationInstance)) {
          if (pv.name == HdInstancerTokens->instanceRotations
                || pv.name == HdInstancerTokens->instanceScales
                || pv.name == HdInstancerTokens->instanceTranslations
                || pv.name == HdInstancerTokens->instanceTransforms)
              continue;

          if (allPrimvars.find(pv.name) != std::cend(allPrimvars)) continue;
          allPrimvars.insert(pv.name);

          // Consider the primvar if it is dirty and is part of the new binding set
          TfToken prevBindingPoint;
          TfToken newBindingPoint;
          if (auto it = previousBinding.find(pv.name); it != std::cend(previousBinding)) prevBindingPoint = it->second;
          if (auto it = updatedPrimvarBinding.find(pv.name); it != std::cend(updatedPrimvarBinding)) newBindingPoint = it->second;

          auto thisInstancer = static_cast<const HdAnariInstancer*>(renderIndex.GetInstancer(instancerId));
          
          if (prevBindingPoint != newBindingPoint || thisInstancer->IsPrimvarDirty(pv.name)) {
              instancePrimvarDescriptors.push_back(pv);
          }
        }
      }

      for (const auto& pv : instancePrimvarDescriptors) {
          if (auto it = updatedPrimvarBinding.find(pv.name); it != std::cend(updatedPrimvarBinding)) {
            const auto& value = instancer->GatherInstancePrimvar(GetId(), pv.name);
            _SetInstanceAttributeArray(it->second, value);
          }
      }
  }


  anari::commitParameters(_anari.device, _anari.instance);

  primvarBinding_ = updatedPrimvarBinding;

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
    instances.push_back(_anari.instance);
  }
}

void HdAnariMesh::_UpdatePrimvarSources(HdSceneDelegate *sceneDelegate,
    const HdPrimvarDescriptorVector &primvarDescriptors,
    HdAnariMaterial::PrimvarBinding primvarBinding)
{
  HD_TRACE_FUNCTION();
  const SdfPath &id = GetId();

  TF_VERIFY(sceneDelegate);

  for (const auto &pvd : primvarDescriptors) {
    if (auto pvbIt = primvarBinding.find(pvd.name);
        pvbIt != cend(primvarBinding)) {
      const auto& value = sceneDelegate->Get(id, pvd.name);
      auto attributeName = pvbIt->second;

      switch (pvd.interpolation) {
      case HdInterpolationConstant: {
        _SetGeometryAttributeConstant(attributeName, value);
        break;
      }
      case HdInterpolationUniform: {
        VtValue perFace;
        meshUtil_->GatherPerFacePrimvar(GetId(), pvd.name, value, trianglePrimitiveParams_, &perFace);
        auto it = primitiveBindingPoints.find(attributeName);
        _SetGeometryAttributeArray(attributeName, it->second, perFace);
        break;
      }
      case HdInterpolationFaceVarying: {
        auto it = faceVaryingBindingPoints.find(attributeName);
        if (it == std::cend(faceVaryingBindingPoints)) {
          TF_CODING_ERROR("%s is not a valid faceVarying geometry attribute\n", attributeName.GetText());
          continue;
        }

        HdVtBufferSource buffer(pvd.name, value);
        VtValue triangulatedPrimvar;
        auto success = meshUtil_->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

        if (success) {
          _SetGeometryAttributeArray(attributeName, it->second, triangulatedPrimvar);  
        } else {
          TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
          _SetGeometryAttributeArray(attributeName, it->second, VtValue());  
        }
        break;
      }
      case HdInterpolationVarying:
      case HdInterpolationVertex: {
        auto it = vertexBindingPoints.find(attributeName);
        if (it == std::cend(vertexBindingPoints)) {
          TF_CODING_ERROR("%s is not a valid vertex geometry attribute\n", attributeName.GetText());
          continue;
        }
        _SetGeometryAttributeArray(attributeName, it->second, value);
        break;
      }
      default:
        break;
      }
    }
  }
}

void HdAnariMesh::_UpdateComputationPrimvarSources(HdSceneDelegate *sceneDelegate,
    const HdExtComputationPrimvarDescriptorVector& computationPrimvarDescriptors,
    const HdExtComputationUtils::ValueStore &computationPrimvarSources,
    HdAnariMaterial::PrimvarBinding primvarBinding)
{
  HD_TRACE_FUNCTION();
  const SdfPath &id = GetId();

  TF_VERIFY(sceneDelegate);

  for (const auto &pvd : computationPrimvarDescriptors) {
    if (auto pvbIt = primvarBinding.find(pvd.name);
        pvbIt != cend(primvarBinding)) {
      const auto& valueIt = computationPrimvarSources.find(pvd.name);
      if (!TF_VERIFY(valueIt != std::cend(computationPrimvarSources))) continue;

      const auto& value = valueIt->second;

      auto attributeName = pvbIt->second;

      switch (pvd.interpolation) {
      case HdInterpolationConstant: {
        _SetGeometryAttributeConstant(attributeName, value);
        break;
      }
      case HdInterpolationUniform: {
        VtValue perFace;
        meshUtil_->GatherPerFacePrimvar(GetId(), pvd.name, value, trianglePrimitiveParams_, &perFace);
        auto it = primitiveBindingPoints.find(attributeName);
        _SetGeometryAttributeArray(attributeName, it->second, perFace);
        break;
      }
      case HdInterpolationFaceVarying: {
        auto it = faceVaryingBindingPoints.find(attributeName);
        if (it == std::cend(faceVaryingBindingPoints)) {
          TF_CODING_ERROR("%s is not a valid faceVarying geometry attribute\n", attributeName.GetText());
          continue;
        }

        HdVtBufferSource buffer(pvd.name, value);
        VtValue triangulatedPrimvar;
        auto success = meshUtil_->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
            buffer.GetNumElements(),
            buffer.GetTupleType().type,
            &triangulatedPrimvar);

        if (success) {
          _SetGeometryAttributeArray(attributeName, it->second, triangulatedPrimvar);  
        } else {
          TF_CODING_ERROR("     ERROR: could not triangulate face-varying data\n");
          _SetGeometryAttributeArray(attributeName, it->second, VtValue());  
        }
        break;
      }
      case HdInterpolationVarying:
      case HdInterpolationVertex: {
        auto it = vertexBindingPoints.find(attributeName);
        if (it == std::cend(vertexBindingPoints)) {
          TF_CODING_ERROR("%s is not a valid vertex geometry attribute\n", attributeName.GetText());
          continue;
        }
        _SetGeometryAttributeArray(attributeName, it->second, value);
        break;
      }
      default:
        break;
      }
    }
  }
}

void HdAnariMesh::_SetGeometryAttributeConstant(const TfToken &attributeName, const VtValue& value)
{
  auto d = _anari.device;
  auto g = _anari.geometry;

  GfVec4f attrV(0.f, 0.f, 0.f, 1.f);
  if (_GetVtValueAsAttribute(value, attrV)) {
    anari::setParameter(d, g, attributeName.GetText(), attrV);
    geometryBindingPoints_.emplace(attributeName, attributeName);
    TF_DEBUG_MSG(HD_ANARI_MESH, "Assigning constant %s to mesh %s\n", attributeName.GetText(), GetId().GetText());
  }
  else
  {
    geometryBindingPoints_.erase(attributeName);
    TF_DEBUG_MSG(HD_ANARI_MESH, "Clearing constant %s on mesh %s\n", attributeName.GetText(), GetId().GetText());
    anari::unsetParameter(d, g, attributeName.GetText());
  }
}

void HdAnariMesh::_SetGeometryAttributeArray(const TfToken &attributeName, const TfToken& bindingPoint, const VtValue& value)
{
  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (!value.IsEmpty() && _GetVtArrayBufferData(value, &data, &size, &type)) {
    TF_DEBUG_MSG(HD_ANARI_MESH, "Assigning geometry primvar %s to %s on mesh %s\n", attributeName.GetText(), bindingPoint.GetText(), GetId().GetText());
    anari::setParameterArray1D(_anari.device, _anari.geometry, bindingPoint.GetText(), type, data, size);
    geometryBindingPoints_.emplace(attributeName, attributeName);
  } else {
    geometryBindingPoints_.erase(attributeName);
    anari::unsetParameter(_anari.device, _anari.geometry, bindingPoint.GetText());
    TF_DEBUG_MSG(HD_ANARI_MESH, "Clearing geometry primvar %s on %s on mesh %s\n", attributeName.GetText(), bindingPoint.GetText(), GetId().GetText());
  }
}

void HdAnariMesh::_SetInstanceAttributeArray(const TfToken &attributeName, const VtValue& value)
{
  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (!value.IsEmpty() && _GetVtArrayBufferData(value, &data, &size, &type)) {
    TF_DEBUG_MSG(HD_ANARI_MESH, "Assigning instance primvar %s to mesh %s\n", attributeName.GetText(), GetId().GetText());
    anari::setParameterArray1D(_anari.device, _anari.instance, attributeName.GetText(), type, data, size);
    instanceBindingPoints_.emplace(attributeName, attributeName);
  } else {
    instanceBindingPoints_.erase(attributeName);
    anari::unsetParameter(_anari.device, _anari.instance, attributeName.GetText());
    TF_DEBUG_MSG(HD_ANARI_MESH, "Clearing instance primvar %s on mesh %s\n", attributeName.GetText(), GetId().GetText());
  }
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
