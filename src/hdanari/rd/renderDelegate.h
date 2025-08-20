// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "renderParam.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <pxr/pxr.h>

#include <anari/anari_cpp.hpp>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariRenderParam;

#define HDANARI_RENDER_SETTINGS_TOKENS                                         \
  (ambientRadiance)(ambientColor)(                                             \
      ambientSamples)(sampleLimit)(maxRayDepth)(denoise)(renderSubtype)(debugMethod)

TF_DECLARE_PUBLIC_TOKENS(
    HdAnariRenderSettingsTokens, HDANARI_RENDER_SETTINGS_TOKENS);

class HdAnariRenderDelegate final : public HdRenderDelegate
{
 public:
  HdAnariRenderDelegate();
  HdAnariRenderDelegate(HdRenderSettingsMap const &settingsMap);
  ~HdAnariRenderDelegate() override;

  HdAnariRenderParam *GetRenderParam() const override;

  const TfTokenVector &GetSupportedRprimTypes() const override;
  const TfTokenVector &GetSupportedSprimTypes() const override;
  const TfTokenVector &GetSupportedBprimTypes() const override;

  HdResourceRegistrySharedPtr GetResourceRegistry() const override;

  HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

  bool IsPauseSupported() const override;
  bool Pause() override;
  bool Resume() override;

  HdRenderPassSharedPtr CreateRenderPass(
      HdRenderIndex *index, HdRprimCollection const &collection) override;

  HdInstancer *CreateInstancer(
      HdSceneDelegate *delegate, SdfPath const &id) override;
  void DestroyInstancer(HdInstancer *instancer) override;

  HdRprim *CreateRprim(TfToken const &typeId, SdfPath const &rprimId) override;
  void DestroyRprim(HdRprim *rPrim) override;

  HdSprim *CreateSprim(TfToken const &typeId, SdfPath const &sprimId) override;
  HdSprim *CreateFallbackSprim(TfToken const &typeId) override;
  void DestroySprim(HdSprim *sPrim) override;

  HdBprim *CreateBprim(TfToken const &typeId, SdfPath const &bprimId) override;
  HdBprim *CreateFallbackBprim(TfToken const &typeId) override;
  void DestroyBprim(HdBprim *bPrim) override;

  void CommitResources(HdChangeTracker *tracker) override;

  TfToken GetMaterialBindingPurpose() const override
  {
    return HdTokens->full;
  }

  HdAovDescriptor GetDefaultAovDescriptor(TfToken const &name) const override;

  VtDictionary GetRenderStats() const override;

 private:
  static const TfTokenVector SUPPORTED_RPRIM_TYPES;
  static const TfTokenVector SUPPORTED_SPRIM_TYPES;
  static const TfTokenVector SUPPORTED_BPRIM_TYPES;

  static std::mutex _mutexResourceRegistry;
  static std::atomic_int _counterResourceRegistry;
  static HdResourceRegistrySharedPtr _resourceRegistry;

  // This class does not support copying.
  HdAnariRenderDelegate(const HdAnariRenderDelegate &) = delete;
  HdAnariRenderDelegate &operator=(const HdAnariRenderDelegate &) = delete;

  void Initialize();

  std::shared_ptr<HdAnariRenderParam> _renderParam;

  HdRenderSettingDescriptorList _settingDescriptors;
};

PXR_NAMESPACE_CLOSE_SCOPE
