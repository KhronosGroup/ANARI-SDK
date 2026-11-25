// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeviceErrorLog.h"

namespace anari {
namespace cts {

void DeviceErrorLog::record(anari::StatusSeverity severity, const char *message)
{
  if (severity != ANARI_SEVERITY_ERROR
      && severity != ANARI_SEVERITY_FATAL_ERROR)
    return;
  std::lock_guard<std::mutex> lock(m_mutex);
  m_errors.emplace_back(message ? message : "");
}

size_t DeviceErrorLog::size() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_errors.size();
}

std::vector<std::string> DeviceErrorLog::since(size_t mark) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (mark >= m_errors.size())
    return {};
  return {m_errors.begin() + mark, m_errors.end()};
}

} // namespace cts
} // namespace anari
