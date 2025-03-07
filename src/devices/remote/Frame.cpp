// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"

namespace remote {

void Frame::resizeColor(uint32_t width, uint32_t height, ANARIDataType type)
{
  size_t newSize =
      type == ANARI_UNKNOWN ? 0 : width * height * anari::sizeOf(type);

  if (size[0] != width || size[1] != height || colorType != type
      || color.size() != newSize) {
    size[0] = width;
    size[1] = height;
    colorType = type;
    color.resize(newSize);
  }
}

void Frame::resizeDepth(uint32_t width, uint32_t height, ANARIDataType type)
{
  size_t newSize =
      type == ANARI_UNKNOWN ? 0 : width * height * anari::sizeOf(type);

  if (size[0] != width || size[1] != height || depthType != type
      || depth.size() != newSize) {
    size[0] = width;
    size[1] = height;
    depthType = type;
    depth.resize(newSize);
  }
}

} // namespace remote
