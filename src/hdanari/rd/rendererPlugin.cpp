// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "rendererPlugin.h"

#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include "renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
  HdRendererPluginRegistry::Define<HdAnariRendererPlugin>();
}

HdRenderDelegate *HdAnariRendererPlugin::CreateRenderDelegate()
{
  return new HdAnariRenderDelegate();
}

HdRenderDelegate *HdAnariRendererPlugin::CreateRenderDelegate(
    HdRenderSettingsMap const &settingsMap)
{
  return new HdAnariRenderDelegate(settingsMap);
}

void HdAnariRendererPlugin::DeleteRenderDelegate(
    HdRenderDelegate *renderDelegate)
{
  delete renderDelegate;
}

bool HdAnariRendererPlugin::IsSupported(bool /* gpuEnabled */) const
{
  return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
