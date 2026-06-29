// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "FrameReadback.h"
#include "generators/ColorPalette.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace anari {
namespace cts {

namespace {

const char *frameChannelParameter(Channel channel)
{
  switch (channel) {
  case Channel::Color:
    return "channel.color";
  case Channel::Depth:
    return "channel.depth";
  case Channel::Albedo:
    return "channel.albedo";
  case Channel::Normal:
    return "channel.normal";
  case Channel::PrimitiveId:
    return "channel.primitiveId";
  case Channel::ObjectId:
    return "channel.objectId";
  case Channel::InstanceId:
    return "channel.instanceId";
  }
  return nullptr;
}

class ScopedFrameMapping
{
 public:
  ScopedFrameMapping(
      anari::Device device, anari::Frame frame, const char *channel)
      : m_device(device), m_frame(frame), m_channel(channel)
  {
    m_mapped = anari::map<uint8_t>(m_device, m_frame, m_channel);
  }

  ~ScopedFrameMapping()
  {
    if (m_mapped.data)
      anari::unmap(m_device, m_frame, m_channel);
  }

  ScopedFrameMapping(const ScopedFrameMapping &) = delete;
  ScopedFrameMapping &operator=(const ScopedFrameMapping &) = delete;

  MappedFrameDescriptor descriptor() const
  {
    return {m_mapped.data, m_mapped.width, m_mapped.height, m_mapped.pixelType};
  }

 private:
  anari::Device m_device{nullptr};
  anari::Frame m_frame{nullptr};
  const char *m_channel{nullptr};
  anari::MappedFrameData<uint8_t> m_mapped;
};

struct TypeTable
{
  const ANARIDataType *data{nullptr};
  size_t size{0};

  const ANARIDataType *begin() const
  {
    return data;
  }

  const ANARIDataType *end() const
  {
    return size == 0 ? data : data + size;
  }
};

template <size_t N>
TypeTable typeTable(const std::array<ANARIDataType, N> &types)
{
  return {types.data(), types.size()};
}

TypeTable supportedTypes(Channel channel)
{
  static constexpr std::array colorTypes = {
      ANARI_UFIXED8_VEC4, ANARI_UFIXED8_RGBA_SRGB, ANARI_FLOAT32_VEC4};
  static constexpr std::array depthTypes = {ANARI_FLOAT32};
  static constexpr std::array albedoTypes = {
      ANARI_UFIXED8_VEC3, ANARI_UFIXED8_RGB_SRGB, ANARI_FLOAT32_VEC3};
  static constexpr std::array normalTypes = {
      ANARI_FIXED16_VEC3, ANARI_FLOAT32_VEC3};
  static constexpr std::array idTypes = {ANARI_UINT32};

  switch (channel) {
  case Channel::Color:
    return typeTable(colorTypes);
  case Channel::Depth:
    return typeTable(depthTypes);
  case Channel::Albedo:
    return typeTable(albedoTypes);
  case Channel::Normal:
    return typeTable(normalTypes);
  case Channel::PrimitiveId:
  case Channel::ObjectId:
  case Channel::InstanceId:
    return typeTable(idTypes);
  }
  return {};
}

bool checkedMultiply(size_t a, size_t b, size_t &result)
{
  if (a != 0 && b > std::numeric_limits<size_t>::max() / a)
    return false;
  result = a * b;
  return true;
}

uint8_t toU8(float value)
{
  const float scaled = value * 255.f;
  return static_cast<uint8_t>(
      scaled < 0.f ? 0.f : (scaled > 255.f ? 255.f : scaled));
}

} // namespace

FrameReadbackResult decodeFrameChannel(Channel channel,
    const MappedFrameDescriptor &mapped,
    uint32_t expectedWidth,
    uint32_t expectedHeight,
    float depthScale)
{
  if (mapped.data == nullptr) {
    FrameReadbackResult result;
    result.error = FrameReadbackError::NullData;
    result.detail =
        std::string("channel.") + channelName(channel) + " mapped data is null";
    return result;
  }
  if (mapped.width != expectedWidth || mapped.height != expectedHeight) {
    FrameReadbackResult result;
    result.error = FrameReadbackError::DimensionMismatch;
    result.detail = std::string("channel.") + channelName(channel)
        + " mapped dimensions mismatch: expected "
        + std::to_string(expectedWidth) + "x" + std::to_string(expectedHeight)
        + ", got " + std::to_string(mapped.width) + "x"
        + std::to_string(mapped.height);
    return result;
  }
  const auto types = supportedTypes(channel);
  if (std::find(types.begin(), types.end(), mapped.pixelType) == types.end()) {
    FrameReadbackResult result;
    result.error = FrameReadbackError::UnsupportedType;
    result.detail = std::string("channel.") + channelName(channel)
        + " returned unsupported mapped pixel type "
        + anari::toString(mapped.pixelType) + " (supported:";
    for (const auto type : types)
      result.detail += std::string(" ") + anari::toString(type);
    result.detail += ")";
    return result;
  }

  size_t pixelCount = 0;
  size_t outputBytes = 0;
  size_t inputBytes = 0;
  if (!checkedMultiply(mapped.width, mapped.height, pixelCount)
      || !checkedMultiply(pixelCount, size_t{4}, outputBytes)
      || !checkedMultiply(
          pixelCount, anari::sizeOf(mapped.pixelType), inputBytes)) {
    FrameReadbackResult result;
    result.error = FrameReadbackError::SizeOverflow;
    result.detail = std::string("channel.") + channelName(channel)
        + " mapped buffer size calculation overflow";
    return result;
  }

  FrameReadbackResult result;
  result.image.width = mapped.width;
  result.image.height = mapped.height;
  result.image.rgba.assign(outputBytes, 0);

  if (channel == Channel::Color) {
    switch (mapped.pixelType) {
    case ANARI_UFIXED8_VEC4:
    case ANARI_UFIXED8_RGBA_SRGB:
      std::memcpy(result.image.rgba.data(), mapped.data, inputBytes);
      break;
    case ANARI_FLOAT32_VEC4: {
      const auto *pixels = static_cast<const float *>(mapped.data);
      for (size_t i = 0; i < outputBytes; ++i)
        result.image.rgba[i] = toU8(pixels[i]);
      break;
    }
    }
  } else if (channel == Channel::Depth) {
    const auto *pixels = static_cast<const float *>(mapped.data);
    const float scale = depthScale > 0.f ? 1.f / depthScale : 0.f;
    auto *dst = result.image.rgba.data();
    for (size_t i = 0; i < pixelCount; ++i, dst += 4) {
      const uint8_t gray =
          std::isfinite(pixels[i]) ? toU8(pixels[i] * scale) : 255;
      dst[0] = gray;
      dst[1] = gray;
      dst[2] = gray;
      dst[3] = 255;
    }
  } else if (channel == Channel::Albedo) {
    auto *dst = result.image.rgba.data();
    switch (mapped.pixelType) {
    case ANARI_UFIXED8_VEC3:
    case ANARI_UFIXED8_RGB_SRGB: {
      const auto *pixels = static_cast<const uint8_t *>(mapped.data);
      for (size_t i = 0; i < pixelCount; ++i, pixels += 3, dst += 4) {
        dst[0] = pixels[0];
        dst[1] = pixels[1];
        dst[2] = pixels[2];
        dst[3] = 255;
      }
      break;
    }
    case ANARI_FLOAT32_VEC3: {
      const auto *pixels = static_cast<const float *>(mapped.data);
      for (size_t i = 0; i < pixelCount; ++i, pixels += 3, dst += 4) {
        dst[0] = toU8(pixels[0]);
        dst[1] = toU8(pixels[1]);
        dst[2] = toU8(pixels[2]);
        dst[3] = 255;
      }
      break;
    }
    }
  } else if (channel == Channel::Normal) {
    auto *dst = result.image.rgba.data();
    switch (mapped.pixelType) {
    case ANARI_FIXED16_VEC3: {
      const auto *pixels = static_cast<const int16_t *>(mapped.data);
      for (size_t i = 0; i < pixelCount; ++i, pixels += 3, dst += 4) {
        for (int c = 0; c < 3; ++c) {
          const float value = std::max(pixels[c] / 32767.f, -1.f) * 0.5f + 0.5f;
          dst[c] = toU8(value);
        }
        dst[3] = 255;
      }
      break;
    }
    case ANARI_FLOAT32_VEC3: {
      const auto *pixels = static_cast<const float *>(mapped.data);
      for (size_t i = 0; i < pixelCount; ++i, pixels += 3, dst += 4) {
        for (int c = 0; c < 3; ++c)
          dst[c] = toU8(pixels[c] * 0.5f + 0.5f);
        dst[3] = 255;
      }
      break;
    }
    }
  } else if (channel == Channel::PrimitiveId || channel == Channel::ObjectId
      || channel == Channel::InstanceId) {
    const auto *pixels = static_cast<const uint32_t *>(mapped.data);
    auto *dst = result.image.rgba.data();
    for (size_t i = 0; i < pixelCount; ++i, dst += 4) {
      const auto color = scenes::colors::getColorFromPalette(pixels[i]);
      dst[0] = toU8(color.x);
      dst[1] = toU8(color.y);
      dst[2] = toU8(color.z);
      dst[3] = 255;
    }
  }

  return result;
}

FrameReadbackResult readFrameChannel(anari::Device device,
    anari::Frame frame,
    Channel channel,
    uint32_t expectedWidth,
    uint32_t expectedHeight,
    float depthScale)
{
  ScopedFrameMapping mapped(device, frame, frameChannelParameter(channel));
  return decodeFrameChannel(
      channel, mapped.descriptor(), expectedWidth, expectedHeight, depthScale);
}

} // namespace cts
} // namespace anari
