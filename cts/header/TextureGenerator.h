// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/linalg.h"

#include <vector>

namespace cts {

class TextureGenerator
{
 public:
  static std::vector<anari::math::float4> generateGreyScale(size_t resolution);
  static std::vector<anari::math::float4> generateCheckerBoard(size_t resolution);
  static std::vector<anari::math::float4> generateRGBRamp(
      size_t resolution);

  static std::vector<anari::math::float4> generateCheckerBoardNormalMap(
      size_t resolution);

  static std::vector<anari::math::float4> generateCheckerBoardHDR(
      size_t resolution);

  static anari::math::float3 convertColorToNormal(anari::math::float3 color);
  static float convertNormalToColor(float input, bool isZ);
  static uint8_t convertShortNormalToColor(int16_t input, bool isZ);
};
} // namespace cts
