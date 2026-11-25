// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#ifndef HDANARI_RENDERSETTINGS_H
#define HDANARI_RENDERSETTINGS_H

#include <pxr/imaging/hd/renderSettings.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

PXR_NAMESPACE_OPEN_SCOPE

// Bridges a UsdRenderSettings prim's namespaced settings into the render
// delegate's setting map. Without this, scene-authored settings (renderSubtype
// and every introspected renderer parameter) reach Hydra only as a
// renderSettings Bprim, never the legacy GetRenderSetting() map the render pass
// reads from, so `usdrecord --renderSettingsPrimPath` is silently ignored.
class HdAnariRenderSettings final : public HdRenderSettings
{
 public:
  explicit HdAnariRenderSettings(SdfPath const &id);
  ~HdAnariRenderSettings() override;

 protected:
  void _Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      const HdDirtyBits *dirtyBits) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDANARI_RENDERSETTINGS_H
