// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide_gpu {

struct Light : public Object
{
  Light(HelideGPUDeviceGlobalState *d);
  ~Light() override = default;
  static Light *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Light *, ANARI_LIGHT);
