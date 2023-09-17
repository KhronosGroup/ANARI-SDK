// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

namespace importers {

struct HDRImage
{
  bool load(std::string fileName);

  unsigned width;
  unsigned height;
  unsigned numComponents;
  std::vector<float> pixel;
};

} // namespace importers
