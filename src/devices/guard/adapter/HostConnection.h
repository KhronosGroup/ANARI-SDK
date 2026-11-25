// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/Wire.h"

#include <string>

namespace anari_guard {

// A wrapped device identifier, '[subtype@]library', taken apart.
struct WrappedDevice
{
  std::string library;
  std::string subtype{"default"};
};

// Resolve the subtype given to anariNewDevice(guard, ...) (or to an extension
// query): 'default' reads ANARI_GUARD_WRAPPED_DEVICE. False, with 'error' set,
// if nothing usable is named.
bool resolveWrappedDevice(
    const char *subtype, WrappedDevice &out, std::string &error);

// The adapter's side of one running host.
struct HostConnection
{
  int call{-1};
  int event{-1};
  platform::HostProcess process;

  // Find and start anariGuardHost and check its hello.
  bool start(std::string &error);

  // End a host that is no longer wanted and release everything held for it.
  void kill();
  // Close both channels and stop watching the host, which must have been
  // reaped (or is being left to end by itself).
  void closeEndpoints();
};

} // namespace anari_guard
