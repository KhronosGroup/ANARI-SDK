// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mesh.h"
#include "instancer.h"
#include "material.h"
// pxr
#include <pxr/base/gf/matrix4d.h>
// std
#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(HdAnariTokens, (st)(UVMap));

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

  anari::retain(d, d);

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
  anari::release(_anari.device, _anari.device);
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

void HdAnariMesh::Finalize(HdRenderParam *renderParam)
{
  // no-op?
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

  if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
    SetMaterialId(sceneDelegate->GetMaterialId(id));
    auto *material = static_cast<const HdAnariMaterial *>(
        renderIndex.GetSprim(HdPrimTypeTokens->material, GetMaterialId()));
    if (material) {
      anari::setParameter(_anari.device,
          _anari.surface,
          "material",
          material->GetANARIMaterial());
    } else {
      anari::setParameter(_anari.device,
          _anari.surface,
          "material",
          renderParam->GetANARIDefaultMaterial());
    }
    anari::commitParameters(_anari.device, _anari.surface);
  }

  // Vertex data //

  if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
    anari::unsetParameter(_anari.device, _anari.geometry, "vertex.position");
    VtValue value = sceneDelegate->Get(id, HdTokens->points);
    auto points = value.Get<VtVec3fArray>();
    if (!points.empty()) {
      anari::setParameterArray1D(_anari.device,
          _anari.geometry,
          "vertex.position",
          points.cdata(),
          points.size());
    }
    anari::commitParameters(_anari.device, _anari.geometry);

    anari::setParameter(
        _anari.device, _anari.surface, "id", uint32_t(GetPrimId()));
    anari::commitParameters(_anari.device, _anari.surface);
  }

  // Triangle indices //

  bool recomputePrimvars = false;
  if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
    _topology = HdMeshTopology(GetMeshTopology(sceneDelegate), 0);
    _meshUtil = std::make_unique<HdMeshUtil>(&_topology, id);

    anari::unsetParameter(_anari.device, _anari.geometry, "primitive.index");

    VtVec3iArray triangulatedIndices;
    VtIntArray trianglePrimitiveParams;
    _meshUtil->ComputeTriangleIndices(
        &triangulatedIndices, &trianglePrimitiveParams);
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
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)
      || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor)
      || HdChangeTracker::IsPrimvarDirty(
          *dirtyBits, id, HdTokens->displayOpacity)) {
    _UpdatePrimvarSources(sceneDelegate, *dirtyBits);
  }

  // Transforms //

  if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)
      || HdChangeTracker::IsInstancerDirty(*dirtyBits, id)) {
    _ReleaseAnariInstances();

    auto baseTransform = GfMatrix4f(sceneDelegate->GetTransform(id));

    VtMatrix4dArray transforms;
    if (!GetInstancerId().IsEmpty()) {
      HdInstancer *instancer = renderIndex.GetInstancer(GetInstancerId());
      transforms =
          static_cast<HdAnariInstancer *>(instancer)->ComputeInstanceTransforms(
              id);
    } else {
      transforms.push_back(GfMatrix4d(1.0));
    }

    _anari.instances.reserve(transforms.size());
    for (size_t i = 0; i < transforms.size(); i++) {
      const GfMatrix4d &transform = transforms[i];
      GfMatrix4f mat = baseTransform * GfMatrix4f(transform);
      auto inst = anari::newObject<anari::Instance>(_anari.device, "transform");
      anari::setParameter(_anari.device, inst, "group", _anari.group);
      anari::setParameter(_anari.device, inst, "transform", mat);
      anari::setParameter(_anari.device, inst, "id", uint32_t(i));
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

void HdAnariMesh::_UpdatePrimvarSources(
    HdSceneDelegate *sceneDelegate, HdDirtyBits dirtyBits)
{
  HD_TRACE_FUNCTION();
  const SdfPath &id = GetId();

  TF_VERIFY(sceneDelegate);

  HdPrimvarDescriptorVector primvars;
  for (size_t i = 0; i < HdInterpolationCount; ++i) {
    auto interp = static_cast<HdInterpolation>(i);
    primvars = GetPrimvarDescriptors(sceneDelegate, interp);
    for (const HdPrimvarDescriptor &pv : primvars) {
      if (pv.name == HdTokens->points)
        continue;

      auto value = sceneDelegate->Get(id, pv.name);
      const bool constant = interp == HdInterpolationConstant;
      const bool perPrimitive = interp == HdInterpolationUniform;
      const bool perVertex =
          interp == HdInterpolationVertex || interp == HdInterpolationVarying;
      const bool faceVarying = interp == HdInterpolationFaceVarying;

      if ((pv.role == HdPrimvarRoleTokens->textureCoordinate
              || pv.name == HdAnariTokens->UVMap)
          && HdChangeTracker::IsPrimvarDirty(
              dirtyBits, id, HdAnariTokens->UVMap)) {
        if (faceVarying) {
          HdVtBufferSource buffer(pv.name, value);
          VtValue triangulatedPrimvar;
          auto success =
              _meshUtil->ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(),
                  buffer.GetNumElements(),
                  buffer.GetTupleType().type,
                  &triangulatedPrimvar);
          if (success) {
            _SetGeometryAttributeArray(
                "faceVarying.attribute0", triangulatedPrimvar);
          } else
            TF_CODING_ERROR("ERROR: could not triangulate face-varying data\n");
        } else if (perVertex)
          _SetGeometryAttributeArray("vertex.attribute0", value);
        else if (perPrimitive)
          _SetGeometryAttributeArray("primitive.attribute0", value);
      } else if (pv.name == HdTokens->normals) {
        if (value.IsHolding<VtVec3fArray>() && perVertex)
          _SetGeometryAttributeArray("vertex.normal", value);
        else {
          anari::unsetParameter(
              _anari.device, _anari.geometry, "vertex.normal");
        }
      } else if (pv.name == HdTokens->displayColor
          && HdChangeTracker::IsPrimvarDirty(
              dirtyBits, id, HdTokens->displayColor)) {
        if (constant)
          _SetGeometryAttributeConstant("color", value);
        else if (perPrimitive)
          _SetGeometryAttributeArray("primitive.color", value);
        else if (perVertex)
          _SetGeometryAttributeArray("vertex.color", value);
        else if (interp == HdInterpolationFaceVarying)
          printf("HdAnariMesh: HdInterpolationFaceVarying not yet supported\n");
        else if (interp == HdInterpolationInstance)
          printf("HdAnariMesh: HdInterpolationInstance not yet supported\n");
      }

      anari::commitParameters(_anari.device, _anari.geometry);
    }
  }
}

void HdAnariMesh::_SetGeometryAttributeConstant(
    const char *attributeName, VtValue v)
{
  auto d = _anari.device;
  auto g = _anari.geometry;

  GfVec4f attrV(0.f, 0.f, 0.f, 1.f);
  if (_GetVtValueAsAttribute(v, attrV))
    anari::setParameter(d, g, attributeName, attrV);
  else
    anari::unsetParameter(d, g, attributeName);
}

void HdAnariMesh::_SetGeometryAttributeArray(
    const char *attributeName, VtValue v)
{
  if (!v.IsArrayValued())
    return;

  auto d = _anari.device;
  auto g = _anari.geometry;

  anari::DataType type = ANARI_UNKNOWN;
  const void *data = nullptr;
  size_t size = 0;

  if (_GetVtArrayBufferData(v, &data, &size, &type))
    anari::setParameterArray1D(d, g, attributeName, type, data, size);
  else
    anari::unsetParameter(d, g, attributeName);
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
