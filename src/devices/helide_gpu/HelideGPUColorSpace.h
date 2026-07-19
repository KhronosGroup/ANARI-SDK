// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HelideGPUMath.h"
// anari
#include <anari/anari.h>
// std
#include <cmath>
#include <cstdint>
#include <cstring>

namespace helide_gpu {

// Helpers for correct sRGB-to-linear conversion on the CPU.
//
// Helium's readAsAttributeValue() applies pow(v, 1/2.2) for SRGB element
// types, which is the *inverse* of what is needed (it encodes rather than
// decodes sRGB). These functions bypass that conversion, reading the raw
// array bytes and applying the correct pow(v, 2.2) decode.

inline bool isElementTypeSRGB(ANARIDataType type)
{
  return type == ANARI_UFIXED8_R_SRGB || type == ANARI_UFIXED8_RA_SRGB
      || type == ANARI_UFIXED8_RGB_SRGB || type == ANARI_UFIXED8_RGBA_SRGB;
}

inline int srgbComponentCount(ANARIDataType type)
{
  switch (type) {
  case ANARI_UFIXED8_R_SRGB:
    return 1;
  case ANARI_UFIXED8_RA_SRGB:
    return 2;
  case ANARI_UFIXED8_RGB_SRGB:
    return 3;
  case ANARI_UFIXED8_RGBA_SRGB:
    return 4;
  default:
    return 0;
  }
}

// Read a single element from a flat byte buffer of sRGB UFIXED8 data,
// returning a linear-space vec4 (alpha is always linear, defaults to 1.0).
inline vec4 srgbBytesToLinear(const uint8_t *bytes, int numComponents)
{
  vec4 result(0.f, 0.f, 0.f, 1.f);
  for (int c = 0; c < std::min(numComponents, 3); ++c)
    result[c] = std::pow(bytes[c] / 255.f, 2.2f);
  if (numComponents == 4)
    result.w = bytes[3] / 255.f;
  return result;
}

} // namespace helide_gpu
