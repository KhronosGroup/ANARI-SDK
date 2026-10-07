// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "hdanari/rd/material/uvTextureTransform.h"

namespace {

using Vec4 = std::array<float, 4>;
using Output = HdAnariUsdUVTextureOutput;

constexpr Vec4 kDefaultScale = {1.f, 1.f, 1.f, 1.f};
constexpr Vec4 kDefaultBias = {0.f, 0.f, 0.f, 0.f};

// What an ANARI sampler returns for 'texel': outTransform (column-major) times
// the texel, plus outOffset.
Vec4 sample(const HdAnariSamplerOutTransform &t, const Vec4 &texel)
{
  Vec4 out = t.offset;
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      out[row] += t.transform[col * 4 + row] * texel[col];
  return out;
}

void checkVec4(const Vec4 &actual, const Vec4 &expected)
{
  for (int i = 0; i < 4; ++i)
    CHECK(actual[i] == Approx(expected[i]));
}

} // namespace

TEST_CASE("UsdUVTexture outputs swizzle with default scale/bias",
    "[hdanari_uvTextureTransform]")
{
  const Vec4 texel = {0.1f, 0.2f, 0.3f, 0.4f};
  auto out = [&](Output o) {
    return sample(
        HdAnariMakeUsdUVTextureOutTransform(o, kDefaultScale, kDefaultBias),
        texel);
  };

  checkVec4(out(Output::RGB), {0.1f, 0.2f, 0.3f, 1.f});
  checkVec4(out(Output::R), {0.1f, 0.1f, 0.1f, 0.1f});
  checkVec4(out(Output::G), {0.2f, 0.2f, 0.2f, 0.2f});
  checkVec4(out(Output::B), {0.3f, 0.3f, 0.3f, 0.3f});
  checkVec4(out(Output::A), {0.4f, 0.4f, 0.4f, 0.4f});
  checkVec4(out(Output::Other), texel);
}

TEST_CASE("UsdUVTexture normal map scale/bias decodes to tangent space",
    "[hdanari_uvTextureTransform]")
{
  auto t = HdAnariMakeUsdUVTextureOutTransform(
      Output::RGB, {2.f, 2.f, 2.f, 2.f}, {-1.f, -1.f, -1.f, -1.f});

  // A flat texel decodes to the unperturbed normal; alpha stays 1.
  checkVec4(sample(t, {0.5f, 0.5f, 1.f, 0.f}), {0.f, 0.f, 1.f, 1.f});
  checkVec4(sample(t, {0.f, 1.f, 0.5f, 0.3f}), {-1.f, 1.f, 0.f, 1.f});
}

TEST_CASE(
    "UsdUVTexture single-channel output applies that channel's "
    "scale/bias",
    "[hdanari_uvTextureTransform]")
{
  const Vec4 scale = {3.f, 0.5f, 7.f, 9.f};
  const Vec4 bias = {-3.f, 0.25f, -7.f, -9.f};
  const Vec4 texel = {0.1f, 0.6f, 0.3f, 0.4f};

  checkVec4(sample(HdAnariMakeUsdUVTextureOutTransform(Output::G, scale, bias),
                texel),
      {0.55f, 0.55f, 0.55f, 0.55f});
  checkVec4(sample(HdAnariMakeUsdUVTextureOutTransform(Output::A, scale, bias),
                texel),
      {-5.4f, -5.4f, -5.4f, -5.4f});
}

TEST_CASE("UsdUVTexture rgb output keeps alpha at 1 under scale/bias",
    "[hdanari_uvTextureTransform]")
{
  auto t = HdAnariMakeUsdUVTextureOutTransform(
      Output::RGB, {0.5f, 2.f, 4.f, 8.f}, {0.1f, 0.2f, 0.3f, 0.4f});
  checkVec4(sample(t, {1.f, 0.5f, 0.25f, 0.f}), {0.6f, 1.2f, 1.3f, 1.f});
}

TEST_CASE("UsdUVTexture unswizzled output applies scale/bias per channel",
    "[hdanari_uvTextureTransform]")
{
  auto t = HdAnariMakeUsdUVTextureOutTransform(
      Output::Other, {0.5f, 2.f, 4.f, 8.f}, {0.1f, 0.2f, 0.3f, 0.4f});
  checkVec4(sample(t, {1.f, 0.5f, 0.25f, 0.5f}), {0.6f, 1.2f, 1.3f, 4.4f});
}
