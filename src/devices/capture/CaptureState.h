// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// std
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace capture_device {

// The ANARI API functions that reach a device, whose calls the device counts.
// Each is 'anari' followed by the name here.
#define CAPTURE_API_CALLS(X)                                                   \
  X(CommitParameters)                                                          \
  X(DeviceGetProcAddress)                                                      \
  X(DiscardFrame)                                                              \
  X(FrameReady)                                                                \
  X(GetObjectInfo)                                                             \
  X(GetObjectSubtypes)                                                         \
  X(GetParameterInfo)                                                          \
  X(GetProperty)                                                               \
  X(MapArray)                                                                  \
  X(MapFrame)                                                                  \
  X(MapParameterArray1D)                                                       \
  X(MapParameterArray2D)                                                       \
  X(MapParameterArray3D)                                                       \
  X(NewArray1D)                                                                \
  X(NewArray2D)                                                                \
  X(NewArray3D)                                                                \
  X(NewCamera)                                                                 \
  X(NewFrame)                                                                  \
  X(NewGeometry)                                                               \
  X(NewGroup)                                                                  \
  X(NewInstance)                                                               \
  X(NewLight)                                                                  \
  X(NewMaterial)                                                               \
  X(NewObject)                                                                 \
  X(NewRenderer)                                                               \
  X(NewSampler)                                                                \
  X(NewSpatialField)                                                           \
  X(NewSurface)                                                                \
  X(NewVolume)                                                                 \
  X(NewWorld)                                                                  \
  X(Release)                                                                   \
  X(RenderFrame)                                                               \
  X(Retain)                                                                    \
  X(SetParameter)                                                              \
  X(UnmapArray)                                                                \
  X(UnmapFrame)                                                                \
  X(UnmapParameterArray)                                                       \
  X(UnsetAllParameters)                                                        \
  X(UnsetParameter)

enum class ApiCall : uint32_t
{
#define CAPTURE_API_CALL_ENUM(Name) Name,
  CAPTURE_API_CALLS(CAPTURE_API_CALL_ENUM)
#undef CAPTURE_API_CALL_ENUM
      COUNT
};

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

  // The calls the application has made to the device so far, by function
  std::array<std::atomic<uint64_t>, size_t(ApiCall::COUNT)> apiCalls;

  CaptureState(ANARIDevice d);

  // The functions called at least once, as ("anari<Name>", calls), sorted by
  // name
  std::vector<std::pair<std::string, uint64_t>> callCounts() const;
};

inline CaptureState *asCaptureState(helium::BaseGlobalDeviceState *s)
{
  return (CaptureState *)s;
}

} // namespace capture_device
