// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"

namespace helide_gpu {

Light::Light(HelideGPUDeviceGlobalState *s) : Object(ANARI_LIGHT, s) {}

Light *Light::createInstance(
    std::string_view /*subtype*/, HelideGPUDeviceGlobalState *s)
{
  return (Light *)new UnknownObject(ANARI_LIGHT, s);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Light *);
