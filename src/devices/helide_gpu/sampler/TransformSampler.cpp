// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransformSampler.h"

namespace helide_gpu {

TransformSampler::TransformSampler(HelideGPUDeviceGlobalState *s) : Sampler(s) {}

void TransformSampler::commitParameters()
{
  readCommonParameters();
}

int TransformSampler::samplerMode() const
{
  return 3;
}

bool TransformSampler::isValid() const
{
  return true;
}

} // namespace helide_gpu
