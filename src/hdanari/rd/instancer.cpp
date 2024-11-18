// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "instancer.h"

#include "debugCodes.h"
#include "sampler.h"

// pxr
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/iterator.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/trace/staticKeyData.h>
#include <pxr/base/trace/trace.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/perfLog.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/imaging/hf/perfLog.h>
#include <pxr/usd/sdf/path.h>
// std
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

HdAnariInstancer::HdAnariInstancer(HdSceneDelegate *delegate, SdfPath const &id)
    : HdInstancer(delegate, id)
{
  TF_DEBUG_MSG(
      HD_ANARI_RD_INSTANCER, "Creating instancer with id %s\n", id.GetText());
}

HdAnariInstancer::~HdAnariInstancer()
{
  TF_FOR_ALL(it, _primvarMap)
  {
    delete it->second;
  }
  _primvarMap.clear();
}

void HdAnariInstancer::Sync(HdSceneDelegate *delegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  TF_DEBUG_MSG(
      HD_ANARI_RD_INSTANCER, "Syncing instancer at %s\n", GetId().GetText());
  _UpdateInstancer(delegate, dirtyBits);

  if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId())) {
    _SyncPrimvars(delegate, *dirtyBits);
  }
}

void HdAnariInstancer::_SyncPrimvars(
    HdSceneDelegate *delegate, HdDirtyBits dirtyBits)
{
  HD_TRACE_FUNCTION();
  HF_MALLOC_TAG_FUNCTION();

  // Store dirty bits so they canbe queried by instanciated prims.
  dirtyBits_ = dirtyBits;

  SdfPath const &id = GetId();

  HdPrimvarDescriptorVector primvars =
      delegate->GetPrimvarDescriptors(id, HdInterpolationInstance);

  for (HdPrimvarDescriptor const &pv : primvars) {
    if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name)) {
      VtValue value = delegate->Get(id, pv.name);
      if (!value.IsEmpty()) {
        if (auto it = _primvarMap.find(pv.name); it != end(_primvarMap)) {
          delete it->second;
        }
        _primvarMap[pv.name] = new HdVtBufferSource(pv.name, value);
      }
    }
  }
}

VtMatrix4dArray HdAnariInstancer::ComputeInstanceTransforms(
    SdfPath const &prototypeId)
{
  HD_TRACE_FUNCTION();
  HF_MALLOC_TAG_FUNCTION();

  // The transforms for this level of instancer are computed by:
  // foreach(index : indices) {
  //     instancerTransform
  //     * hydra:instanceTranslations(index)
  //     * hydra:instanceRotations(index)
  //     * hydra:instanceScales(index)
  //     * hydra:instanceTransforms(index)
  // }
  // If any transform isn't provided, it's assumed to be the identity.

  const GfMatrix4d &instancerTransform =
      GetDelegate()->GetInstancerTransform(GetId());
  const VtIntArray &instanceIndices =
      GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

  VtMatrix4dArray transforms(instanceIndices.size());
  for (size_t i = 0; i < instanceIndices.size(); ++i) {
    transforms[i] = instancerTransform;
  }

  // "hydra:instanceTranslations" holds a translation vector for each index.
  if (_primvarMap.count(HdInstancerTokens->instanceTranslations) > 0) {
    HdAnariBufferSampler sampler(
        *_primvarMap[HdInstancerTokens->instanceTranslations]);
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
      GfVec3f translate;
      if (sampler.Sample(instanceIndices[i], &translate)) {
        GfMatrix4d translateMat(1);
        translateMat.SetTranslate(GfVec3d(translate));
        transforms[i] = translateMat * transforms[i];
      }
    }
  }

  // "hydra:instanceRotations" holds a quaternion in <real, i, j, k>
  // format for each index.
  if (_primvarMap.count(HdInstancerTokens->instanceRotations) > 0) {
    HdAnariBufferSampler sampler(
        *_primvarMap[HdInstancerTokens->instanceRotations]);
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
      GfVec4f quat;
      if (sampler.Sample(instanceIndices[i], &quat)) {
        GfMatrix4d rotateMat(1);
        rotateMat.SetRotate(GfQuatd(quat[0], quat[1], quat[2], quat[3]));
        transforms[i] = rotateMat * transforms[i];
      }
      GfVec4f quatd;
      if (sampler.Sample(instanceIndices[i], &quatd)) {
        GfMatrix4d rotateMat(1);
        rotateMat.SetRotate(GfQuatd(quatd[0], quatd[1], quatd[2], quatd[3]));
        transforms[i] = rotateMat * transforms[i];
      }
      GfQuath quath;
      if (sampler.Sample(instanceIndices[i], &quath)) {
        GfMatrix4d rotateMat(1);
        rotateMat.SetRotate(GfQuatd(quath));
        transforms[i] = rotateMat * transforms[i];
      }
    }
  }

  // "hydra:instanceScales" holds an axis-aligned scale vector for each index.
  if (_primvarMap.count(HdInstancerTokens->instanceScales) > 0) {
    HdAnariBufferSampler sampler(
        *_primvarMap[HdInstancerTokens->instanceScales]);
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
      GfVec3f scale;
      if (sampler.Sample(instanceIndices[i], &scale)) {
        GfMatrix4d scaleMat(1);
        scaleMat.SetScale(GfVec3d(scale));
        transforms[i] = scaleMat * transforms[i];
      }
    }
  }

  // "hydra:instanceTransforms" holds a 4x4 transform matrix for each index.
  if (_primvarMap.count(HdInstancerTokens->instanceTransforms) > 0) {
    HdAnariBufferSampler sampler(
        *_primvarMap[HdInstancerTokens->instanceTransforms]);
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
      GfMatrix4d instanceTransform;
      if (sampler.Sample(instanceIndices[i], &instanceTransform)) {
        transforms[i] = instanceTransform * transforms[i];
      }
    }
  }

  if (GetParentId().IsEmpty()) {
    return transforms;
  }

  HdInstancer *parentInstancer =
      GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
  if (!TF_VERIFY(parentInstancer)) {
    return transforms;
  }

  // The transforms taking nesting into account are computed by:
  // parentTransforms = parentInstancer->ComputeInstanceTransforms(GetId())
  // foreach (parentXf : parentTransforms, xf : transforms) {
  //     parentXf * xf
  // }
  const VtMatrix4dArray &parentTransforms =
      static_cast<HdAnariInstancer *>(parentInstancer)
          ->ComputeInstanceTransforms(GetId());

  VtMatrix4dArray final(parentTransforms.size() * transforms.size());
  for (size_t i = 0; i < parentTransforms.size(); ++i) {
    for (size_t j = 0; j < transforms.size(); ++j) {
      final[i * transforms.size() + j] = transforms[j] * parentTransforms[i];
    }
  }
  return final;
}

TfTokenVector HdAnariInstancer::GetPrimvarNames() const
{
  TfTokenVector names;
  for (const auto &pvm : _primvarMap) {
    names.push_back(pvm.first);
  }

  return names;
}

template <typename T>
VtValue HdAnariInstancer::GatherInstancePrimvar(const SdfPath &prototypeId,
    const TfToken &primvarName,
    HdType dataType) const
{
  const auto &instanceIndices =
      GetDelegate()->GetInstanceIndices(GetId(), prototypeId);
  auto instanceCount = instanceIndices.size();

  VtArray<T> result;

  if (auto it = _primvarMap.find(primvarName); it != cend(_primvarMap)) {
    auto maxIndex = it->second->GetNumElements();
    const auto &values = static_cast<const T *>(it->second->GetData());
    result.reserve(instanceCount);
    for (auto instanceIndex : instanceIndices) {
      if (instanceIndex < maxIndex) {
        result.push_back(values[instanceIndex]);
      } else {
        // FIXME: Should some kind of error once thing?
        TF_WARN(
            "Primvar %s.%s has not enough values to match its instancing configuration.\n",
            prototypeId.GetText(),
            primvarName.GetText());
        result.push_back({});
      }
    }
  }

  if (GetParentId().IsEmpty()) {
    // No nested instancing.
    return VtValue(result);
  }

  // Recursive instancing, here we go...
  auto instancerId = GetParentId();
  auto patternSize = instanceIndices.size();
  // How many times samples need to be replicated to match hierarchical
  // instancing. Should be one if primvar is on the leaf instancer
  auto multiplicativeFactor = 1;
  while (!instancerId.IsEmpty()) {
    const auto &instanceIndices =
        GetDelegate()->GetInstanceIndices(instancerId, prototypeId);

    auto instancer = static_cast<const HdAnariInstancer *>(
        GetDelegate()->GetRenderIndex().GetInstancer(instancerId));

    if (result.empty()) {
      // Build a new pattern based on current indices. Each index needs to be
      // repeated cumulatedSamplesCount times. OR something like that... Most
      // probably we need to reset multiplicative factor to one and set (even if
      // unused) the cumulated samples count.

      if (auto it = instancer->_primvarMap.find(primvarName);
          it != cend(instancer->_primvarMap)) {
        result.reserve(patternSize * instanceIndices.size());

        auto maxIndex = it->second->GetNumElements();
        const auto &values = static_cast<const T *>(it->second->GetData());
        for (auto instanceIndex : instanceIndices) {
          if (instanceIndex < maxIndex) {
            std::fill_n(
                std::back_inserter(result), patternSize, values[instanceIndex]);
          } else {
            // FIXME: Should some kind of error once thing?
            TF_WARN(
                "Primvar %s.%s has not enough values to match its instancing configuration.\n",
                prototypeId.GetText(),
                primvarName.GetText());
            std::fill_n(std::back_inserter(result), patternSize, T{});
          }
        }
      }

      patternSize *= instanceIndices.size();
    } else {
      // We already have a pattern. Make sure it is sized correctly so each
      // instance (recusively) gets the right value applied
      multiplicativeFactor *= instanceIndices.size();
    }
    instancerId = instancer->GetParentId();
  }

  if (result.empty())
    return VtValue();

  result.reserve(patternSize * multiplicativeFactor);
  for (int j = 1; j < multiplicativeFactor; ++j) {
    std::copy_n(std::cbegin(result), patternSize, std::back_inserter(result));
  }

  return VtValue(result);
}

VtValue HdAnariInstancer::GatherInstancePrimvar(
    const SdfPath &prototypeId, const TfToken &primvarName) const
{
  // First try and gather type information for that primvar. Not sure which type
  // is supposed to win in case of a disagreement between multiple levels of the
  // intancing hierarchy, let's go for the first one for now.

  HdType dataType = HdTypeInvalid;
  if (auto it = _primvarMap.find(primvarName); it != cend(_primvarMap)) {
    dataType = it->second->GetTupleType().type;
  } else {
    for (auto instancerId = GetParentId(); !instancerId.IsEmpty();
        instancerId = GetDelegate()
            ->GetRenderIndex()
            .GetInstancer(instancerId)
            ->GetParentId()) {
      auto instancer = static_cast<const HdAnariInstancer *>(
          GetDelegate()->GetRenderIndex().GetInstancer(instancerId));
      if (auto it = instancer->_primvarMap.find(primvarName);
          it != cend(instancer->_primvarMap)) {
        dataType = it->second->GetTupleType().type;
        break;
      }
    }
  }

  switch (dataType) {
  case HdTypeFloat:
    return GatherInstancePrimvar<float>(prototypeId, primvarName, dataType);
    break;
  case HdTypeFloatVec2:
    return GatherInstancePrimvar<GfVec2f>(prototypeId, primvarName, dataType);
    break;
  case HdTypeFloatVec3:
    return GatherInstancePrimvar<GfVec3f>(prototypeId, primvarName, dataType);
    break;
  case HdTypeFloatVec4:
    return GatherInstancePrimvar<GfVec4f>(prototypeId, primvarName, dataType);
    break;
  case HdTypeDouble:
    return GatherInstancePrimvar<double>(prototypeId, primvarName, dataType);
    break;
  case HdTypeDoubleVec2:
    return GatherInstancePrimvar<GfVec2d>(prototypeId, primvarName, dataType);
    break;
  case HdTypeDoubleVec3:
    return GatherInstancePrimvar<GfVec3d>(prototypeId, primvarName, dataType);
    break;
  case HdTypeDoubleVec4:
    return GatherInstancePrimvar<GfVec4d>(prototypeId, primvarName, dataType);
    break;
  case HdTypeInvalid:
    // Invalid means we did not find a matching primvar.
    break;
  default:
    TF_CODING_ERROR("Unsupported primvar type for instance gathering [%s.%s]",
        prototypeId.GetText(),
        primvarName.GetText());
    break;
    break;
  }

  return VtValue();
}

bool HdAnariInstancer::IsPrimvarDirty(const TfToken &name) const
{
  bool isDirty = false;
  if (name == HdTokens->points) {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyPoints) != 0;
  } else if (name == HdTokens->velocities) {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyPoints) != 0;
  } else if (name == HdTokens->accelerations) {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyPoints) != 0;
  } else if (name == HdTokens->nonlinearSampleCount) {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyPoints) != 0;
  } else if (name == HdTokens->normals) {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyNormals) != 0;
  } else if (name == HdTokens->widths) {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyWidths) != 0;
  } else {
    isDirty = (dirtyBits_ & HdChangeTracker::DirtyPrimvar) != 0;
  }

  return isDirty;
}

PXR_NAMESPACE_CLOSE_SCOPE
