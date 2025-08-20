// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "debugCodes.h"

#include "pxr/base/tf/registryManager.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfDebug)
{
  TF_DEBUG_ENVIRONMENT_SYMBOL(HDANARI_SDR, "HDANARI_SDR");
}

PXR_NAMESPACE_CLOSE_SCOPE
