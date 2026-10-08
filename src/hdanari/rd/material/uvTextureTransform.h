// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>

// Kept free of USD so it can be unit tested without it, which is why this uses
// a plain namespace rather than PXR_NAMESPACE_OPEN_SCOPE.

namespace hdanari {

// The UsdUVTexture output a material input is connected through.
enum class UsdUVTextureOutput
{
  RGB,
  R,
  G,
  B,
  A,
  RGBA, // passed through unswizzled; also used for unrecognized outputs
};

// A sampler's 'outTransform' (column-major FLOAT32_MAT4) and 'outOffset'.
struct SamplerOutTransform
{
  std::array<float, 16> transform;
  std::array<float, 4> offset;
};

// The sampler output transform reproducing a UsdUVTexture: its 'scale' and
// 'bias' are applied to the texel first (texel * scale + bias), then the
// connected output's swizzle S (with offset s0):
//   transform = S * diag(scale), offset = S * bias + s0
SamplerOutTransform makeUsdUVTextureOutTransform(UsdUVTextureOutput output,
    const std::array<float, 4> &scale,
    const std::array<float, 4> &bias);

} // namespace hdanari
