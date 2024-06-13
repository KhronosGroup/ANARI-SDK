// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// pxr
#include <pxr/base/tf/hashmap.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/pxr.h>
// std
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariInstancer : public HdInstancer
{
 public:
  HdAnariInstancer(HdSceneDelegate *delegate, SdfPath const &id);
  ~HdAnariInstancer();

  VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId);

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

 private:
  void _SyncPrimvars(HdSceneDelegate *delegate, HdDirtyBits dirtyBits);

  TfHashMap<TfToken, std::shared_ptr<HdVtBufferSource>, TfToken::HashFunctor>
      _primvarMap;
};

PXR_NAMESPACE_CLOSE_SCOPE
