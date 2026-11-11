// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Material.h"

namespace helide_gpu {

// The matte and physicallyBased subtypes, which share the color, opacity and
// alpha mode parameters
struct Matte : public Material
{
  Matte(HelideGPUDeviceGlobalState *s, bool physicallyBased);
  ~Matte() override = default;
  void commitParameters() override;

 private:
  bool m_physicallyBased{false};
};

} // namespace helide_gpu
