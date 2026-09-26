// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// helium's sRGB transfer functions: *_SRGB array elements decode with the
// piecewise IEC 61966-2-1 curve (the one GPU sRGB texture formats implement),
// alpha stays linear, and the frame encoder is that curve's inverse.

#include "catch.hpp"

#include "helium/helium_math.h"

#include <cmath>
#include <cstdint>

namespace {

float srgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

anari::math::float4 decode(const uint8_t *bytes, ANARIDataType type)
{
  return helium::math::readAsAttributeValueFlat(bytes, type, 0);
}

} // namespace

SCENARIO(
    "sRGB array elements decode with the IEC 61966-2-1 curve", "[helium_srgb]")
{
  GIVEN("UFIXED8_RGB_SRGB texels at codes across the range")
  {
    for (int code : {0, 1, 5, 10, 11, 64, 128, 200, 255}) {
      const uint8_t rgb[3] = {uint8_t(code), uint8_t(code), uint8_t(code)};
      const auto v = decode(rgb, ANARI_UFIXED8_RGB_SRGB);
      const float expected = srgbToLinear(code / 255.f);
      INFO("code " << code);
      REQUIRE(v.x == Approx(expected).margin(1e-6f));
      REQUIRE(v.y == Approx(expected).margin(1e-6f));
      REQUIRE(v.z == Approx(expected).margin(1e-6f));
      REQUIRE(v.w == 1.f);
    }
  }

  GIVEN("the dark end, where pow(v, 2.2) differs most")
  {
    const uint8_t r[1] = {10};
    REQUIRE(
        decode(r, ANARI_UFIXED8_R_SRGB).x == Approx(0.003035f).margin(1e-5f));
  }
}

SCENARIO("sRGB alpha stays linear", "[helium_srgb]")
{
  GIVEN("a UFIXED8_RGBA_SRGB texel with alpha 128")
  {
    const uint8_t rgba[4] = {128, 128, 128, 128};
    const auto v = decode(rgba, ANARI_UFIXED8_RGBA_SRGB);
    REQUIRE(v.x == Approx(srgbToLinear(128 / 255.f)).margin(1e-6f));
    REQUIRE(v.w == Approx(128 / 255.f).margin(1e-6f));
  }

  GIVEN("a UFIXED8_RA_SRGB texel with alpha 128 (alpha is the 2nd channel)")
  {
    const uint8_t ra[2] = {128, 128};
    const auto v = decode(ra, ANARI_UFIXED8_RA_SRGB);
    REQUIRE(v.x == Approx(srgbToLinear(128 / 255.f)).margin(1e-6f));
    REQUIRE(v.y == Approx(128 / 255.f).margin(1e-6f));
  }
}

SCENARIO("the sRGB frame encoder inverts the decoder", "[helium_srgb]")
{
  GIVEN("every 8-bit code, decoded and re-encoded")
  {
    for (int code = 0; code < 256; ++code) {
      const uint8_t rgba[4] = {
          uint8_t(code), uint8_t(code), uint8_t(code), 255};
      const auto linear = decode(rgba, ANARI_UFIXED8_RGBA_SRGB);
      const uint32_t packed = helium::math::cvt_color_to_uint32_srgb(linear);
      const auto back = helium::math::cvt_color_to_float4(packed);
      INFO("code " << code);
      REQUIRE(std::lround(back.x * 255.f) == code);
      REQUIRE(std::lround(back.w * 255.f) == 255);
    }
  }

  GIVEN("mid grey in linear light")
  {
    // Linear 0.214 is sRGB 0.5 (code 127/128); pow(v, 1/2.2) gives 0.496.
    const auto packed = helium::math::cvt_color_to_uint32_srgb(
        anari::math::float4(0.21404f, 0.21404f, 0.21404f, 1.f));
    const auto back = helium::math::cvt_color_to_float4(packed);
    REQUIRE(std::abs(back.x * 255.f - 127.5f) <= 1.f);
  }
}
