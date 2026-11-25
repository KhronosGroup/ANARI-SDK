// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Shared by the guard_ test programs: a check that fails the test, and the
// host process seen from outside.

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#include <dirent.h>
#include <sys/types.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(                                                            \
          stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

namespace guard_test {

// The pid of a process named 'name' whose parent is 'parent'; -1 if none.
inline pid_t findChild(pid_t parent, const std::string &name)
{
  pid_t found = -1;
  DIR *proc = opendir("/proc");
  if (!proc)
    return -1;
  while (dirent *entry = readdir(proc)) {
    char *end = nullptr;
    long pid = std::strtol(entry->d_name, &end, 10);
    if (*end != '\0' || pid <= 0)
      continue;
    // "pid (comm) state ppid ..."; comm may itself contain parentheses
    std::ifstream stat("/proc/" + std::string(entry->d_name) + "/stat");
    std::string line;
    std::getline(stat, line);
    auto open = line.find('(');
    auto close = line.rfind(')');
    if (open == std::string::npos || close == std::string::npos)
      continue;
    char state = 0;
    long ppid = -1;
    if (std::sscanf(line.c_str() + close + 1, " %c %ld", &state, &ppid) != 2)
      continue;
    if (ppid == parent && state != 'Z'
        && line.substr(open + 1, close - open - 1) == name) {
      found = pid_t(pid);
      break;
    }
  }
  closedir(proc);
  return found;
}

// Wait up to 'seconds' for 'done' to hold.
template <typename F>
inline bool waitFor(F &&done, double seconds = 10.0)
{
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (!done()) {
    if (std::chrono::steady_clock::now() > deadline)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

// How many regions (shared memory made by the guard device) the process 'pid'
// has mapped; -1 if that cannot be read.
inline int countRegions(pid_t pid)
{
  std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
  if (!maps)
    return -1;
  int count = 0;
  std::string line;
  while (std::getline(maps, line)) {
    if (line.find("memfd:anari-guard-region") != std::string::npos)
      count++;
  }
  return count;
}

inline bool processExists(pid_t pid)
{
  return kill(pid, 0) == 0;
}

} // namespace guard_test
