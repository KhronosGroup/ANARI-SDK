// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace helide_gpu {

struct Matte : public Material
{
  Matte(HelideGPUDeviceGlobalState *s);
  ~Matte() override = default;
  void commitParameters() override;
};

} // namespace helide_gpu
