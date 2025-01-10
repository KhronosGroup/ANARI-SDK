// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace remote {

template <typename T>
inline void writeToVoidP(void *_p, T v)
{
  T *p = (T *)_p;
  *p = v;
}

struct MessageType
{
  enum
  {
    NewDevice,
    NewObject,
    NewArray,
    DeviceHandle,
    SetParam,
    UnsetParam,
    UnsetAllParams,
    CommitParams,
    Release,
    Retain,
    ArrayData,
    MapArray,
    ArrayMapped,
    UnmapArray,
    ArrayUnmapped,
    RenderFrame,
    FrameReady,
    FrameIsReady,
    GetProperty,
    Property,
    GetObjectSubtypes,
    ObjectSubtypes,
    GetObjectInfo,
    ObjectInfo,
    GetParameterInfo,
    ParameterInfo,
    ChannelColor,
    ChannelDepth,
  };
};

inline const char *toString(unsigned mt)
{
  switch (mt) {
  case MessageType::NewDevice:
    return "NewDevice";
  case MessageType::NewObject:
    return "NewObject";
  case MessageType::NewArray:
    return "NewArray";
  case MessageType::DeviceHandle:
    return "DeviceHandle";
  case MessageType::SetParam:
    return "SetParam";
  case MessageType::UnsetParam:
    return "UnsetParam";
  case MessageType::UnsetAllParams:
    return "UnsetAllParams";
  case MessageType::CommitParams:
    return "CommitParams";
  case MessageType::Release:
    return "Release";
  case MessageType::Retain:
    return "Retain";
  case MessageType::ArrayData:
    return "ArrayData";
  case MessageType::MapArray:
    return "MapArray";
  case MessageType::ArrayMapped:
    return "ArrayMapped";
  case MessageType::UnmapArray:
    return "UnmapArray";
  case MessageType::ArrayUnmapped:
    return "ArrayUnmapped";
  case MessageType::RenderFrame:
    return "RenderFrame";
  case MessageType::FrameReady:
    return "FrameReady";
  case MessageType::FrameIsReady:
    return "FrameIsReady";
  case MessageType::GetProperty:
    return "GetProperty";
  case MessageType::Property:
    return "Property";
  case MessageType::GetObjectSubtypes:
    return "GetObjectSubtypes";
  case MessageType::ObjectSubtypes:
    return "ObjectSubtypes";
  case MessageType::GetObjectInfo:
    return "GetObjectInfo";
  case MessageType::ObjectInfo:
    return "ObjectInfo";
  case MessageType::GetParameterInfo:
    return "GetParameterInfo";
  case MessageType::ParameterInfo:
    return "ParameterInfo";
  case MessageType::ChannelColor:
    return "CannelColor";
  case MessageType::ChannelDepth:
    return "ChannelDepth";
  default:
    return "Unknown";
  }
}

typedef uint64_t Handle;

inline std::string prettyNumber(const size_t s)
{
  char buf[1000];
  if (s >= (1000LL * 1000LL * 1000LL * 1000LL)) {
    snprintf(buf, 1000, "%.2fT", s / (1000.f * 1000.f * 1000.f * 1000.f));
  } else if (s >= (1000LL * 1000LL * 1000LL)) {
    snprintf(buf, 1000, "%.2fG", s / (1000.f * 1000.f * 1000.f));
  } else if (s >= (1000LL * 1000LL)) {
    snprintf(buf, 1000, "%.2fM", s / (1000.f * 1000.f));
  } else if (s >= (1000LL)) {
    snprintf(buf, 1000, "%.2fK", s / (1000.f));
  } else {
    snprintf(buf, 1000, "%zi", s);
  }
  return buf;
}

inline std::string prettyBytes(const size_t s)
{
  char buf[1000];
  if (s >= (1024LL * 1024LL * 1024LL * 1024LL)) {
    snprintf(buf, 1000, "%.2fT", s / (1024.f * 1024.f * 1024.f * 1024.f));
  } else if (s >= (1024LL * 1024LL * 1024LL)) {
    snprintf(buf, 1000, "%.2fG", s / (1024.f * 1024.f * 1024.f));
  } else if (s >= (1024LL * 1024LL)) {
    snprintf(buf, 1000, "%.2fM", s / (1024.f * 1024.f));
  } else if (s >= (1024LL)) {
    snprintf(buf, 1000, "%.2fK", s / (1024.f));
  } else {
    snprintf(buf, 1000, "%zi", s);
  }
  return buf;
}

} // namespace remote
