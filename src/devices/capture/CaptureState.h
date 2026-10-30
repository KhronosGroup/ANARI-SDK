// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// std
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace capture_device {

struct CaptureState : public helium::BaseGlobalDeviceState
{
  // Device parameters
  uint32_t inlineArrayLimit{64};
  std::string captureDir;
  // The extensions to advertise, sorted, and the same as a null-terminated list
  std::vector<std::string> extensions;
  std::vector<const char *> extensionList;

  // The public handle of the device that owns this state
  ANARIDevice anariDevice{nullptr};

  // Frames created so far, to number each new frame's capture files
  std::atomic<uint32_t> framesCreated{0};

  CaptureState(ANARIDevice d);
};

inline CaptureState *asCaptureState(helium::BaseGlobalDeviceState *s)
{
  return (CaptureState *)s;
}

} // namespace capture_device
