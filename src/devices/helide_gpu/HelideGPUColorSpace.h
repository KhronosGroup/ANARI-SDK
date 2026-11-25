// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HelideGPUMath.h"
#include "HelideGPUSRGB.h"
// std
#include <cstring>

namespace helide_gpu {

// Read a single element from a flat byte buffer of sRGB UFIXED8 data,
// returning a linear-space vec4 (alpha is always linear, defaults to 1.0).
// See HelideGPUSRGB.h.
inline vec4 srgbBytesToLinear(const uint8_t *bytes, int numComponents)
{
  vec4 result;
  srgbBytesToLinear(bytes, numComponents, &result[0]);
  return result;
}

} // namespace helide_gpu
