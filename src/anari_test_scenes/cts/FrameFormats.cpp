// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "FrameFormats.h"

namespace anari {
namespace cts {

ANARIDataType channelDefaultFormat(Channel channel)
{
  switch (channel) {
  case Channel::Color:
    return ANARI_UFIXED8_RGBA_SRGB;
  case Channel::Depth:
    return ANARI_FLOAT32;
  case Channel::Albedo:
  case Channel::Normal:
    return ANARI_FLOAT32_VEC3;
  case Channel::PrimitiveId:
  case Channel::ObjectId:
  case Channel::InstanceId:
    return ANARI_UINT32;
  }
  return ANARI_UFIXED8_RGBA_SRGB;
}

ANARIDataType colorFormatFromString(const std::string &s)
{
  if (s == "UFIXED8_RGBA_SRGB")
    return ANARI_UFIXED8_RGBA_SRGB;
  if (s == "UFIXED8_VEC4")
    return ANARI_UFIXED8_VEC4;
  if (s == "FLOAT32_VEC4")
    return ANARI_FLOAT32_VEC4;
  return ANARI_UNKNOWN;
}

ANARIDataType albedoFormatFromString(const std::string &s)
{
  if (s == "UFIXED8_VEC3")
    return ANARI_UFIXED8_VEC3;
  if (s == "UFIXED8_RGB_SRGB")
    return ANARI_UFIXED8_RGB_SRGB;
  if (s == "FLOAT32_VEC3")
    return ANARI_FLOAT32_VEC3;
  return ANARI_UNKNOWN;
}

ANARIDataType normalFormatFromString(const std::string &s)
{
  if (s == "FIXED16_VEC3")
    return ANARI_FIXED16_VEC3;
  if (s == "FLOAT32_VEC3")
    return ANARI_FLOAT32_VEC3;
  return ANARI_UNKNOWN;
}

ANARIDataType caseChannelFormat(const Case &c, Channel channel)
{
  // The axis name and string->type mapping that overrides this channel's
  // default, if the channel carries an output-format axis.
  const char *axisName = nullptr;
  ANARIDataType (*fromString)(const std::string &) = nullptr;
  switch (channel) {
  case Channel::Color:
    axisName = "frame_color_type";
    fromString = colorFormatFromString;
    break;
  case Channel::Albedo:
    axisName = "frame_albedo_type";
    fromString = albedoFormatFromString;
    break;
  case Channel::Normal:
    axisName = "frame_normal_type";
    fromString = normalFormatFromString;
    break;
  default:
    break;
  }

  if (axisName) {
    for (const auto &cv : c.values) {
      if (cv.axisName == axisName && cv.value.type() == ANARI_STRING) {
        const ANARIDataType t = fromString(cv.value.getString());
        if (t != ANARI_UNKNOWN)
          return t;
      }
    }
  }
  return channelDefaultFormat(channel);
}

} // namespace cts
} // namespace anari
