// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariRendererPlugin final : public HdRendererPlugin
{
 public:
  HdAnariRendererPlugin() = default;

  HdRenderDelegate *CreateRenderDelegate() override;
  HdRenderDelegate *CreateRenderDelegate(
      HdRenderSettingsMap const &settingsMap) override;
  void DeleteRenderDelegate(HdRenderDelegate *renderDelegate) override;

  bool IsSupported(bool gpuEnabled = true) const override;

 private:
  HdAnariRendererPlugin(const HdAnariRendererPlugin &) = delete;
  HdAnariRendererPlugin &operator=(const HdAnariRendererPlugin &) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE
