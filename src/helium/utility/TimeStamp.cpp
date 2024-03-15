// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TimeStamp.h"
// std
#include <atomic>

namespace helium {

static std::atomic<TimeStamp> g_timeStamp = []() { return 0; }();

TimeStamp newTimeStamp()
{
  return ++g_timeStamp;
}

} // namespace helium