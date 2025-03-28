// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/linalg.h"

#include <vector>

#include "anari_test_scenes_export.h"

namespace anari {
namespace scenes {
class ANARI_TEST_SCENES_INTERFACE TextureGenerator
{
 public:
  static std::vector<anari::math::float4> generateGreyScale(size_t resolution);
  static std::vector<anari::math::float4> generateCheckerBoard(
      size_t resolution);
  static std::vector<anari::math::float4> generateRGBRamp(size_t resolution);

  static std::vector<anari::math::float4> generateCheckerBoardNormalMap(
      size_t resolution);

  static std::vector<anari::math::float3> generateCheckerBoardHDR(
      size_t resolution);

  static anari::math::float3 convertColorToNormal(anari::math::float3 color);
  static float convertNormalToColor(float input, bool isZ);
  static uint8_t convertShortNormalToColor(int16_t input, bool isZ);
};
} // namespace scenes
} // namespace anari
