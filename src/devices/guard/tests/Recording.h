// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// For tests that run the guard device over the recording device
// (RecordingDevice.cpp): where its log is, what it should say, and the status
// messages the application was given.

#include "TestSupport.h"

#include <anari/anari.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

namespace guard_test {

// Point the guard device at the recording device, with a fresh log. Call
// before the library is loaded.
inline std::string useRecordingDevice(const char *subtype = nullptr)
{
  char path[] = "recorder-log-XXXXXX";
  const int fd = mkstemp(path);
  CHECK(fd >= 0);
  close(fd);
  setenv("GUARD_RECORDER_LOG", path, 1);
  const std::string wrapped =
      (subtype ? std::string(subtype) + "@" : std::string()) + "guard_recorder";
  setenv("ANARI_GUARD_WRAPPED_DEVICE", wrapped.c_str(), 1);
  return path;
}

inline std::vector<std::string> readLines(const std::string &path)
{
  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line))
    lines.push_back(line);
  return lines;
}

// The checksum the recording device prints for more than 32 bytes.
inline std::string checksum(const void *data, size_t size)
{
  auto *p = static_cast<const uint8_t *>(data);
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < size; i++)
    h = (h ^ p[i]) * 1099511628211ull;
  char text[32];
  std::snprintf(text, sizeof(text), "fnv=%016llx", (unsigned long long)h);
  return text;
}

// Fails the test, showing the first line that differs.
inline void checkLog(
    const std::string &path, const std::vector<std::string> &expected)
{
  const auto actual = readLines(path);
  for (size_t i = 0; i < expected.size() || i < actual.size(); i++) {
    const std::string e = i < expected.size() ? expected[i] : "<nothing>";
    const std::string a = i < actual.size() ? actual[i] : "<nothing>";
    if (e != a) {
      std::fprintf(stderr,
          "FAILED: the wrapped device's call %zu\n  expected: %s\n  actual:   "
          "%s\n",
          i + 1,
          e.c_str(),
          a.c_str());
      std::exit(1);
    }
  }
  unlink(path.c_str());
}

// Every status message the application is given.
struct StatusLog
{
  struct Message
  {
    ANARIDevice device;
    ANARIObject source;
    ANARIDataType sourceType;
    ANARIStatusSeverity severity;
    ANARIStatusCode code;
    std::string text;
  };

  static void callback(const void *userData,
      ANARIDevice device,
      ANARIObject source,
      ANARIDataType sourceType,
      ANARIStatusSeverity severity,
      ANARIStatusCode code,
      const char *message)
  {
    auto &log = *static_cast<StatusLog *>(const_cast<void *>(userData));
    std::lock_guard<std::mutex> lock(log.mutex);
    log.messages.push_back(
        {device, source, sourceType, severity, code, message});
  }

  // Messages whose text contains 'part'.
  std::vector<Message> matching(const std::string &part)
  {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Message> found;
    for (auto &m : messages) {
      if (m.text.find(part) != std::string::npos)
        found.push_back(m);
    }
    return found;
  }

  size_t count()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return messages.size();
  }

  std::mutex mutex;
  std::vector<Message> messages;
};

} // namespace guard_test
