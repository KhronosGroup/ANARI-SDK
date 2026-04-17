// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"

namespace helide_gpu {

struct TransformSampler : public Sampler
{
  TransformSampler(HelideGPUDeviceGlobalState *s);
  ~TransformSampler() override = default;

  void commitParameters() override;

  int samplerMode() const override;
  bool isValid() const override;
};

} // namespace helide_gpu
