// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "utility/DeferredCommitBuffer.h"
// anari
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <functional>
#include <string>

namespace helium {

using namespace linalg::aliases;
using mat4 = float4x4;

struct BaseGlobalDeviceState
{
  ANARIStatusCallback statusCB{nullptr};
  const void *statusCBUserPtr{nullptr};
  DeferredCommitBuffer commitBuffer;
  std::function<void(int, const std::string &, const void *)> messageFunction;

  BaseGlobalDeviceState(ANARIDevice d);
  virtual ~BaseGlobalDeviceState() = default;
};

} // namespace helium
