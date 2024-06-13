// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "instancer.h"
#include "sampler.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <pxr/base/gf/quaternion.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/staticTokens.h>

#include "hdAnariTypes.h"

PXR_NAMESPACE_OPEN_SCOPE

HdAnariInstancer::HdAnariInstancer(HdSceneDelegate *delegate, SdfPath const &id)
    : HdInstancer(delegate, id)
{}

HdAnariInstancer::~HdAnariInstancer() = default;

void HdAnariInstancer::Sync(HdSceneDelegate *delegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  _UpdateInstancer(delegate, dirtyBits);

  if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId()))
    _SyncPrimvars(delegate, *dirtyBits);
}

void HdAnariInstancer::_SyncPrimvars(
    HdSceneDelegate *delegate, HdDirtyBits dirtyBits)
{
  HD_TRACE_FUNCTION();
  HF_MALLOC_TAG_FUNCTION();

  SdfPath const &id = GetId();

  HdPrimvarDescriptorVector primvars =
      delegate->GetPrimvarDescriptors(id, HdInterpolationInstance);

  for (HdPrimvarDescriptor const &pv : primvars) {
    if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name)) {
      VtValue value = delegate->Get(id, pv.name);
      if (!value.IsEmpty()) {
        _primvarMap[pv.name] =
            std::make_shared<HdVtBufferSource>(pv.name, value);
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

  GfMatrix4d instancerTransform = GetDelegate()->GetInstancerTransform(GetId());
  VtIntArray instanceIndices =
      GetDelegate()->GetInstanceIndices(GetId(), prototypeId);

  VtMatrix4dArray transforms(instanceIndices.size());
  for (size_t i = 0; i < instanceIndices.size(); ++i)
    transforms[i] = instancerTransform;

  // XXX: Remove the variables in 24.05
  TfToken instanceTranslationsToken = HdInstancerTokens->instanceTranslations;
  TfToken instanceRotationsToken = HdInstancerTokens->instanceRotations;
  TfToken instanceScalesToken = HdInstancerTokens->instanceScales;
  TfToken instanceTransformsToken = HdInstancerTokens->instanceTransforms;

#if 0
  if (TfGetEnvSetting(HD_USE_DEPRECATED_INSTANCER_PRIMVAR_NAMES)) {
    instanceTranslationsToken = HdInstancerTokens->translate;
    instanceRotationsToken = HdInstancerTokens->rotate;
    instanceScalesToken = HdInstancerTokens->scale;
    instanceTransformsToken = HdInstancerTokens->instanceTransform;
  }
#endif

  // "hydra:instanceTranslations" holds a translation vector for each index.
  if (_primvarMap.count(instanceTranslationsToken) > 0) {
    HdAnariBufferSampler sampler(*_primvarMap[instanceTranslationsToken]);
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
  if (_primvarMap.count(instanceRotationsToken) > 0) {
    HdAnariBufferSampler sampler(*_primvarMap[instanceRotationsToken]);
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
      GfVec4f quat;
      if (sampler.Sample(instanceIndices[i], &quat)) {
        GfMatrix4d rotateMat(1);
        rotateMat.SetRotate(GfQuatd(quat[0], quat[1], quat[2], quat[3]));
        transforms[i] = rotateMat * transforms[i];
      }
    }
  }

  // "hydra:instanceScales" holds an axis-aligned scale vector for each index.
  if (_primvarMap.count(instanceScalesToken) > 0) {
    HdAnariBufferSampler sampler(*_primvarMap[instanceScalesToken]);
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
  if (_primvarMap.count(instanceTransformsToken) > 0) {
    HdAnariBufferSampler sampler(*_primvarMap[instanceTransformsToken]);
    for (size_t i = 0; i < instanceIndices.size(); ++i) {
      GfMatrix4d instanceTransform;
      if (sampler.Sample(instanceIndices[i], &instanceTransform))
        transforms[i] = instanceTransform * transforms[i];
    }
  }

  if (GetParentId().IsEmpty())
    return transforms;

  HdInstancer *parentInstancer =
      GetDelegate()->GetRenderIndex().GetInstancer(GetParentId());
  if (!TF_VERIFY(parentInstancer))
    return transforms;

  // The transforms taking nesting into account are computed by:
  // parentTransforms = parentInstancer->ComputeInstanceTransforms(GetId())
  // foreach (parentXf : parentTransforms, xf : transforms) {
  //     parentXf * xf
  // }
  VtMatrix4dArray parentTransforms =
      static_cast<HdAnariInstancer *>(parentInstancer)
          ->ComputeInstanceTransforms(GetId());

  VtMatrix4dArray final(parentTransforms.size() * transforms.size());
  for (size_t i = 0; i < parentTransforms.size(); ++i) {
    for (size_t j = 0; j < transforms.size(); ++j)
      final[i * transforms.size() + j] = transforms[j] * parentTransforms[i];
  }
  return final;
}

PXR_NAMESPACE_CLOSE_SCOPE
