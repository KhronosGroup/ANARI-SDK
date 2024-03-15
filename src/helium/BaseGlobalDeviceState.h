// Copyright 2022-2024 The Khronos Group
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
using mat4 = float4x4;

struct BaseGlobalDeviceState
{
  void commitBufferAddObject(BaseObject *o);
  void commitBufferFlush();
  void commitBufferClear();
  TimeStamp commitBufferLastFlush() const;

  // Data //

  ANARIStatusCallback statusCB{nullptr};
  const void *statusCBUserPtr{nullptr};

  std::function<void(int, const std::string &, const void *)> messageFunction;

  BaseGlobalDeviceState(ANARIDevice d);
  virtual ~BaseGlobalDeviceState() = default;

 private:
  DeferredCommitBuffer m_commitBuffer;
  mutable std::mutex m_mutex;

  friend struct BaseObject;
  friend struct BaseDevice;
  friend struct Array;
  struct ObjectCounts
  {
    std::atomic<size_t> frames{0};
    std::atomic<size_t> cameras{0};
    std::atomic<size_t> renderers{0};
    std::atomic<size_t> worlds{0};
    std::atomic<size_t> instances{0};
    std::atomic<size_t> groups{0};
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
