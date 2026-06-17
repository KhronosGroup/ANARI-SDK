// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "renderParam.h"
#include "rendererParameters.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>
#include <pxr/pxr.h>

#include <anari/anari_cpp.hpp>
#include <mutex>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariRenderParam;

// Render settings hdanari owns directly; every other renderer parameter is
// discovered from the device and keyed by its verbatim ANARI name.
//   renderSubtype: selects the ANARI renderer subtype.
//   upAxis:        stage up axis, resolves dome-light poleAxis="scene".
//   exposure/tonemap: applied by hdanari to the color AOV, not forwarded.
// clang-format off
#define HDANARI_RENDER_SETTINGS_TOKENS                                         \
  (exposure) \
  (renderSubtype) \
  (tonemap) \
  (upAxis)
// clang-format on

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

  // Renderer parameters discovered for the active subtype, consumed by the
  // render pass to forward host render settings to the ANARI renderer.
  const HdAnariRendererParamList &GetRendererParameters() const;

  // Re-query parameters and rebuild the render-setting descriptors when the
  // render pass switches the active renderer subtype. No-op if unchanged.
  void SyncActiveRendererSubtype(const std::string &subtype);

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

  TfToken GetMaterialBindingPurpose() const override;

  TfTokenVector GetMaterialRenderContexts() const override;

  TfTokenVector GetShaderSourceTypes() const override;

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

  // Must outlive the device, so it is unloaded in the destructor after
  // _renderParam.
  anari::Library _library{nullptr};

  // Re-query the subtype's parameters and rebuild _settingDescriptors. Caller
  // must hold _settingsMutex.
  void _SetActiveRendererSubtypeLocked(
      anari::Device device, const std::string &subtype);
  void _BuildSettingDescriptorsLocked();

  std::shared_ptr<HdAnariRenderParam> _renderParam;

  // Guards _settingDescriptors / _rendererParams / _activeRendererSubtype,
  // which the render thread rebuilds while the host reads descriptors.
  mutable std::mutex _settingsMutex;
  HdRenderSettingDescriptorList _settingDescriptors;
  HdAnariRendererParamList _rendererParams;
  std::string _activeRendererSubtype;
};

PXR_NAMESPACE_CLOSE_SCOPE
