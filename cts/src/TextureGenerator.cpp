// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TextureGenerator.h"

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
  std::vector<glm::vec4> checkerBoard(resolution * resolution);
  for (size_t i = 0; i < 8; ++i) {
    size_t offset = resolution * i * resolution / 8;
    if (i % 2 == 0) {
      offset += resolution / 8;
    }
    for (size_t j = 0; j < 8; j += 2) {
      for (size_t x = 0; x < resolution / 8; ++x) {
        for (size_t y = 0; y < resolution / 8; ++y) {
          checkerBoard[y + j * resolution / 8 + x * resolution + offset] = {1.0f, 1.0f, 1.0f, 1.0f};
        }
      }
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

} // namespace cts
