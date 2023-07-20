// Copyright 2023 The Khronos Group
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
    ChannelColor,
    ChannelDepth,
  };
};

typedef int64_t Handle;

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
