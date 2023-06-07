// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari/anari_cpp/ext/glm.h"

#include <vector>

namespace cts {

class TextureGenerator
{
 public:
  static std::vector<glm::vec4> generateGreyScale(size_t resolution);
  static std::vector<glm::vec4> generateCheckerBoard(size_t resolution);
  static std::vector<glm::vec4> generateRGBRamp(
      size_t resolution);
};
} // namespace cts
