// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TextureGenerator.h"
#include "ColorPalette.h"

namespace cts {
std::vector<glm::vec4> TextureGenerator::generateGreyScale(size_t resolution) {
  std::vector<glm::vec4> greyscale;
  greyscale.reserve(resolution);
  for (size_t i = 0; i < resolution; ++i) {
    greyscale.push_back({1.0f * i / (resolution - 1),
        1.0f * i / (resolution - 1),
        1.0f * i / (resolution - 1),
        1.0f});
  }
  return greyscale;
}

std::vector<glm::vec4> TextureGenerator::generateCheckerBoard(
    size_t resolution)
{
  size_t counter = 0;
  std::vector<glm::vec4> checkerBoard(resolution * resolution);
  for (size_t i = 0; i < 8; ++i) {
    size_t offset = resolution * i * resolution / 8;
    if (i % 2 == 0) {
      offset += resolution / 8;
    }
    for (size_t j = 0; j < 8; j += 2) {
      for (size_t x = 0; x < resolution / 8; ++x) {
        for (size_t y = 0; y < resolution / 8; ++y) {
          checkerBoard[y + j * resolution / 8 + x * resolution + offset] = glm::vec4(colors::getColorFromPalette(counter), 1.0f);
        }
      }
      ++counter;
    }  
  }
  return checkerBoard;
}

std::vector<glm::vec4>
TextureGenerator::generateRGBRamp(size_t resolution)
{
  std::vector<glm::vec4> RGBRamp(resolution * resolution * resolution);
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

glm::vec3 TextureGenerator::convertColorToNormal(glm::vec3 color) {
  glm::vec3 result;
  result.x = color.x * 2.0f - 1.0f;
  result.y = color.y * 2.0f - 1.0f;
  result.z = color.z * -1.0f;
  result = glm::normalize(result);
  result.x = result.x / 2.0f + 0.5f;
  result.y = result.y / 2.0f + 0.5f;
  result.z = result.z / -2.0f + 0.5f;
  return result;
}

std::vector<glm::vec4> TextureGenerator::generateCheckerBoardNormalMap(
    size_t resolution)
{
  size_t counter = 0;
  glm::vec4 defaultNormal = {0.5f, 0.5f, 1.0f, 1.0f};
  std::vector<glm::vec4> checkerBoard(resolution * resolution);
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
            glm::vec3 color =
                convertColorToNormal(colors::getColorFromPalette(counter));
            checkerBoard[y + j * resolution / 8 + x * resolution + offset + rowOffset] =
                glm::vec4(color, 1.0f);
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
