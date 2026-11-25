// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Tuning aids: two constants of the guard device that the environment can
// override, for measuring what they cost. Each is read once, by the adapter
// and by the host (which inherits the adapter's environment), the first time
// it is needed. Undocumented, and not a promise.

#include "common/Protocol.h"

#include <cstdlib>

namespace anari_guard {

// The value of environment variable 'name' if it is a whole number above
// zero, else 'fallback'.
inline uint64_t tuningValue(const char *name, uint64_t fallback)
{
  const char *text = std::getenv(name);
  if (!text || *text == '\0')
    return fallback;
  char *end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  return *end == '\0' && value > 0 ? uint64_t(value) : fallback;
}

// ANARI_GUARD_INLINE_THRESHOLD, in bytes: the array ceiling. An array of at
// least this size has its contents cross in a region from the start. A
// smaller one crosses inline in its message the first time, and is given a
// region only when it is written again; object arrays and parameter-array
// mappings below the ceiling cross inline every time. Frame channels do not
// go by it: see FRAME_REGION_THRESHOLD.
inline uint64_t arrayCeiling()
{
  static const uint64_t bytes =
      tuningValue("ANARI_GUARD_INLINE_THRESHOLD", DEFAULT_ARRAY_CEILING);
  return bytes;
}

// ANARI_GUARD_PROBE_PERIOD_US, in microseconds: how long the host's reader
// waits for a message, while a render is outstanding, before it has the
// worker ask the wrapped device whether the frame is ready.
inline uint64_t probePeriodMicroseconds()
{
  static const uint64_t microseconds =
      tuningValue("ANARI_GUARD_PROBE_PERIOD_US", DEFAULT_PROBE_PERIOD_US);
  return microseconds;
}

} // namespace anari_guard
