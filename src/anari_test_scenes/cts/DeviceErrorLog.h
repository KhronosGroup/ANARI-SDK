// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// The ERROR and FATAL_ERROR status messages a device reported, from whichever
// thread reported them. The status callback the CTS loads a device with
// records into one, so a behaviour check can fail on a device error
// (TestDef::failOnDeviceErrors).
class DeviceErrorLog
{
 public:
  // Keeps 'message' if 'severity' is ERROR or FATAL_ERROR.
  void record(anari::StatusSeverity severity, const char *message);

  // How many errors have been recorded: a mark for since().
  size_t size() const;

  // The errors recorded after 'mark' (an earlier size()).
  std::vector<std::string> since(size_t mark) const;

 private:
  mutable std::mutex m_mutex;
  std::vector<std::string> m_errors;
};

} // namespace cts
} // namespace anari
