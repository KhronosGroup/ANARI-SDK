// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari_test_scenes_export.h"
// std
#include <cstdint>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// An 8-bit RGBA raster, row-major with row 0 at the top. The common currency
// for rendered channels, ground truth, and metric comparison.
struct Image
{
  uint32_t width{0};
  uint32_t height{0};
  std::vector<uint8_t> rgba; // width * height * 4 bytes

  bool valid() const
  {
    return width > 0 && height > 0
        && rgba.size() == static_cast<size_t>(width) * height * 4;
  }

  size_t pixelCount() const
  {
    return static_cast<size_t>(width) * height;
  }
};

// Load a PNG as RGBA8. Returns an invalid Image (valid() == false) on failure.
ANARI_TEST_SCENES_INTERFACE Image loadPNG(const std::string &path);

// Write an RGBA8 image as a PNG, creating parent directories as needed.
// Returns false on failure (including an invalid image).
ANARI_TEST_SCENES_INTERFACE bool savePNG(
    const std::string &path, const Image &image);

} // namespace cts
} // namespace anari
