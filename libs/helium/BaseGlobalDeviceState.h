// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "utility/DeferredCommitBuffer.h"
// anari
#include <anari/anari.h>
// std
#include <functional>
#include <string>

namespace helium {

struct BaseGlobalDeviceState
{
  ANARIStatusCallback statusCB{nullptr};
  const void *statusCBUserPtr{nullptr};
  DeferredCommitBuffer commitBuffer;
  std::function<void(int, const std::string &, const void *)> messageFunction;

  BaseGlobalDeviceState(ANARIDevice d);
};

} // namespace helium
