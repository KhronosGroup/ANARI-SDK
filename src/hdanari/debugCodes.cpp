// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "debugCodes.h"

#include <pxr/base/tf/registryManager.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
  TF_DEBUG_ENVIRONMENT_SYMBOL(
      HD_ANARI_RD_RENDERDELEGATE, "Trace HdAnari RenderDelegate");
  TF_DEBUG_ENVIRONMENT_SYMBOL(HD_ANARI_RD_INSTANCER, "Trace HdAnari Instancer");
  TF_DEBUG_ENVIRONMENT_SYMBOL(HD_ANARI_RD_MATERIAL, "Trace HdAnari Materials");
}

PXR_NAMESPACE_CLOSE_SCOPE
