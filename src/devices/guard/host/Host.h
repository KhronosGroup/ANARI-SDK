// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/Protocol.h"

namespace anari_guard {

// Sees every call the host executes: before() runs once the message is read
// and before the wrapped device is called, after() once the wrapped device has
// returned and before any reply is sent. A test host hangs its fault hook here.
struct CallObserver
{
  virtual ~CallObserver() = default;
  virtual void before(Op) {}
  virtual void after(Op) {}
};

// Serve one adapter on the two inherited channel endpoints until it releases
// the device or goes away. Returns the process exit code.
int runHost(
    int callEndpoint, int eventEndpoint, CallObserver *observer = nullptr);

} // namespace anari_guard
