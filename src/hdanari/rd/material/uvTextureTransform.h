// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

// Kept free of USD so it can be unit tested without it.

// The UsdUVTexture output a material input is connected through.
enum class HdAnariUsdUVTextureOutput
{
  RGB,
  R,
  G,
  B,
  A,
  Other, // passed through unswizzled
};

// A sampler's 'outTransform' (column-major FLOAT32_MAT4) and 'outOffset'.
struct HdAnariSamplerOutTransform
{
  std::array<float, 16> transform;
  std::array<float, 4> offset;
};

// The sampler output transform reproducing a UsdUVTexture: its 'scale' and
// 'bias' are applied to the texel first (texel * scale + bias), then the
// connected output's swizzle S (with offset s0):
//   transform = S * diag(scale), offset = S * bias + s0
inline HdAnariSamplerOutTransform HdAnariMakeUsdUVTextureOutTransform(
    HdAnariUsdUVTextureOutput output,
    const std::array<float, 4> &scale,
    const std::array<float, 4> &bias)
{
  using Output = HdAnariUsdUVTextureOutput;

  // clang-format off
  HdAnariSamplerOutTransform swizzle{{
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
    }, {0.0f, 0.0f, 0.0f, 0.0f}};
  // clang-format on

  if (output == Output::RGB) {
    // Alpha comes out as 1 rather than the texel's.
    swizzle.transform[15] = 0.0f;
    swizzle.offset[3] = 1.0f;
  } else if (output != Output::Other) {
    // Broadcast the one channel to all four components.
    const int channel = output == Output::R ? 0
        : output == Output::G               ? 1
        : output == Output::B               ? 2
                                            : 3;
    swizzle.transform.fill(0.0f);
    for (int row = 0; row < 4; ++row)
      swizzle.transform[channel * 4 + row] = 1.0f;
  }

  HdAnariSamplerOutTransform result = swizzle;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      const float s = swizzle.transform[col * 4 + row];
      result.transform[col * 4 + row] = s * scale[col];
      result.offset[row] += s * bias[col];
    }
  }
  return result;
}
