// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace anari {
namespace cts {

// A frame output buffer that can be compared against ground truth on its own.
enum class Channel
{
  Color,
  Depth,
  Albedo,
  Normal,
  PrimitiveId,
  ObjectId,
  InstanceId,
};

inline const char *channelName(Channel c)
{
  switch (c) {
  case Channel::Color:
    return "color";
  case Channel::Depth:
    return "depth";
  case Channel::Albedo:
    return "albedo";
  case Channel::Normal:
    return "normal";
  case Channel::PrimitiveId:
    return "primitiveId";
  case Channel::ObjectId:
    return "objectId";
  case Channel::InstanceId:
    return "instanceId";
  }
  return "unknown";
}

} // namespace cts
} // namespace anari
