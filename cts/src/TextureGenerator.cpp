// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TextureGenerator.h"
#include "ColorPalette.h"
#include "PrimitiveGenerator.h"

namespace cts {
std::vector<anari::math::float4> TextureGenerator::generateGreyScale(size_t resolution) {
  std::vector<anari::math::float4> greyscale;
  greyscale.reserve(resolution);
  for (size_t i = 0; i < resolution; ++i) {
    greyscale.push_back({1.0f * i / (resolution - 1),
        1.0f * i / (resolution - 1),
        1.0f * i / (resolution - 1),
        1.0f});
  }
  return greyscale;
}

std::vector<anari::math::float4> TextureGenerator::generateCheckerBoard(
    size_t resolution)
{
  size_t counter = 0;
  std::vector<anari::math::float4> checkerBoard(resolution * resolution);
  for (size_t i = 0; i < 8; ++i) {
    size_t offset = resolution * i * resolution / 8;
    if (i % 2 == 0) {
      offset += resolution / 8;
    }
    for (size_t j = 0; j < 8; j += 2) {
      for (size_t x = 0; x < resolution / 8; ++x) {
        for (size_t y = 0; y < resolution / 8; ++y) {
          checkerBoard[y + j * resolution / 8 + x * resolution + offset] = anari::math::float4(colors::getColorFromPalette(counter), 1.0f);
        }
      }
      ++counter;
    }
  }
  return checkerBoard;
}

std::vector<anari::math::float3> TextureGenerator::generateCheckerBoardHDR(size_t resolution)
{
  PrimitiveGenerator generator(0);
  size_t counter = 0;
  std::vector<anari::math::float3> checkerBoard(resolution * resolution);
  for (size_t i = 0; i < 8; ++i) {
    size_t offset = resolution * i * resolution / 8;
    if (i % 2 == 0) {
      offset += resolution / 8;
    }
    for (size_t j = 0; j < 8; j += 2) {
      anari::math::float3 color = colors::getColorFromPalette(counter);
      color += generator.getRandomFloat(-0.5f, 0.5f);
      color = anari::math::clamp(color, 0.0f, 2.0f);
      for (size_t x = 0; x < resolution / 8; ++x) {
        for (size_t y = 0; y < resolution / 8; ++y) {
          checkerBoard[y + j * resolution / 8 + x * resolution + offset] =
              color;
        }
      }
      ++counter;
    }
  }
  return checkerBoard;
}

std::vector<anari::math::float4>
TextureGenerator::generateRGBRamp(size_t resolution)
{
  std::vector<anari::math::float4> RGBRamp(resolution * resolution * resolution);
  for (size_t x = 0; x < resolution; ++x) {
    for (size_t y = 0; y < resolution; ++y) {
      for (size_t z = 0; z < resolution; ++z) {
        RGBRamp[x + y * resolution + z * resolution * resolution] = {
            1.0f * x / (resolution - 1),
            1.0f * y / (resolution - 1),
            1.0f * z / (resolution - 1),
            1.0f};
      }
    }
  }
  return RGBRamp;
}

uint8_t TextureGenerator::convertShortNormalToColor(int16_t input, bool isZ)
{
  if (isZ) {
    return static_cast<uint8_t>(input / -2.0f + 128);
  } else {
    return static_cast<uint8_t>(input / 2.0f + 128);
  }
}

float TextureGenerator::convertNormalToColor(float input, bool isZ)
{
  if (isZ) {
    return input / -2.0f + 0.5f;
  } else {
    return input / 2.0f + 0.5f;
  }
}

anari::math::float3 TextureGenerator::convertColorToNormal(anari::math::float3 color) {
  anari::math::float3 result;
  result.x = color.x * 2.0f - 1.0f;
  result.y = color.y * 2.0f - 1.0f;
  result.z = color.z * -1.0f;
  result = anari::math::normalize(result);
  result.x = result.x / 2.0f + 0.5f;
  result.y = result.y / 2.0f + 0.5f;
  result.z = result.z / -2.0f + 0.5f;
  return result;
}

std::vector<anari::math::float4> TextureGenerator::generateCheckerBoardNormalMap(
    size_t resolution)
{
  size_t counter = 0;
  anari::math::float4 defaultNormal = {0.5f, 0.5f, 1.0f, 1.0f};
  std::vector<anari::math::float4> checkerBoard(resolution * resolution);
  for (size_t i = 0; i < 8; ++i) {
    size_t offset = resolution * i * resolution / 8;
    size_t rowOffset = 0;
    if (i % 2 == 0) {
      rowOffset = resolution / 8;
    }
    for (size_t j = 0; j < 8; ++j) {
      for (size_t x = 0; x < resolution / 8; ++x) {
        for (size_t y = 0; y < resolution / 8; ++y) {
          if (j % 2 == 0) {
            anari::math::float3 color =
                convertColorToNormal(colors::getColorFromPalette(counter));
            checkerBoard[y + j * resolution / 8 + x * resolution + offset + rowOffset] =
                anari::math::float4(color, 1.0f);
          } else {
            checkerBoard[y + j * resolution / 8 + x * resolution + offset - rowOffset] =
                defaultNormal;
          }
        }
      }
      if (j % 2 == 0) {
        ++counter;
      }
    }
  }
  return checkerBoard;
}

} // namespace cts
