// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Channel.h"
#include "Image.h"
#include "anari_test_scenes_export.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <string>

namespace anari {
namespace cts {

// The descriptor returned by anariMapFrame, separated from the map operation
// so malformed device results can be tested without a deliberately broken
// device.
struct MappedFrameDescriptor
{
  const void *data{nullptr};
  uint32_t width{0};
  uint32_t height{0};
  ANARIDataType pixelType{ANARI_UNKNOWN};
};

enum class FrameReadbackError
{
  None,
  NullData,
  DimensionMismatch,
  UnsupportedType,
  SizeOverflow,
};

// A decoded comparison image or a structured validation/conversion failure.
struct FrameReadbackResult
{
  Image image;
  FrameReadbackError error{FrameReadbackError::None};
  std::string detail;

  explicit operator bool() const
  {
    return error == FrameReadbackError::None;
  }
};

// Validate and decode one mapped Channel into the CTS comparison-image format.
// depthScale is used only for Channel::Depth.
ANARI_TEST_SCENES_INTERFACE FrameReadbackResult decodeFrameChannel(
    Channel channel,
    const MappedFrameDescriptor &mapped,
    uint32_t expectedWidth,
    uint32_t expectedHeight,
    float depthScale = 1.f);

// Map, validate, and decode one frame Channel. The scoped mapping pairs every
// successful map with exactly one unmap, including validation failures and
// exceptions raised while allocating or converting the comparison image.
ANARI_TEST_SCENES_INTERFACE FrameReadbackResult readFrameChannel(
    anari::Device device,
    anari::Frame frame,
    Channel channel,
    uint32_t expectedWidth,
    uint32_t expectedHeight,
    float depthScale = 1.f);

} // namespace cts
} // namespace anari
