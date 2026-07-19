// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "utility/DeferredCommitBuffer.h"
// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace helium {

using namespace linalg::aliases;
using mat4 = anari::math::float4x4;

/*
 * Shared state bag owned by BaseDevice and accessible from every BaseObject
 * without a circular include dependency. Holds the DeferredCommitBuffer,
 * the application-provided status callback, and a message-dispatch function.
 * Device implementors should subclass this to add device-specific context
 * (GPU handles, allocators, render state, etc.) and cast deviceState() to
 * their subtype inside object implementations.
 */
struct BaseGlobalDeviceState
{
  DeferredCommitBuffer commitBuffer;

  // Data //

  ANARIStatusCallback statusCB{nullptr};
  const void *statusCBUserPtr{nullptr};

  std::function<void(int, const std::string &, anari::DataType, const void *)>
      messageFunction;

  BaseGlobalDeviceState(ANARIDevice d);
  virtual ~BaseGlobalDeviceState() = default;

 private:
  friend struct BaseObject;
  friend struct BaseDevice;
  friend struct Array;
  /*
   * Per-type live-object counters, accessible via anariGetProperty on the
   * device. Updated atomically as objects are constructed and destroyed.
   */
  struct ObjectCounts
  {
    std::atomic<size_t> frames{0};
    std::atomic<size_t> cameras{0};
    std::atomic<size_t> renderers{0};
    std::atomic<size_t> worlds{0};
    std::atomic<size_t> instances{0};
    std::atomic<size_t> groups{0};
    std::atomic<size_t> lights{0};
    std::atomic<size_t> surfaces{0};
    std::atomic<size_t> geometries{0};
    std::atomic<size_t> materials{0};
    std::atomic<size_t> samplers{0};
    std::atomic<size_t> volumes{0};
    std::atomic<size_t> spatialFields{0};
    std::atomic<size_t> arrays{0};
    std::atomic<size_t> unknown{0};
  } objectCounts;
};

} // namespace helium
