// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// What the guard device does not forward, because it means nothing in
// another process: memory on a GPU, native handles, pointers. The adapter
// leaves these extensions out of every extension list it returns and refuses
// what goes with them.

#include <string>
#include <vector>

namespace anari_guard {

inline bool isOutOfScopeExtension(const std::string &extension)
{
  // Arrays over CUDA device memory, frame channels mapped as CUDA device
  // memory, and an MPI communicator handed over as a pointer.
  return extension == "ANARI_NV_ARRAY_CUDA"
      || extension == "ANARI_NV_FRAME_BUFFERS_CUDA"
      || extension == "ANARI_KHR_DATA_PARALLEL_MPI";
}

inline void removeOutOfScopeExtensions(std::vector<std::string> &extensions)
{
  std::vector<std::string> kept;
  for (auto &e : extensions) {
    if (!isOutOfScopeExtension(e))
      kept.push_back(std::move(e));
  }
  extensions = std::move(kept);
}

// ANARI_NV_FRAME_BUFFERS_CUDA: every channel has a counterpart, named with
// this suffix, whose mapping is a pointer to device memory.
inline bool isOutOfScopeChannel(const std::string &channel)
{
  const std::string suffix = "CUDA";
  return channel.size() >= suffix.size()
      && channel.compare(channel.size() - suffix.size(), suffix.size(), suffix)
      == 0;
}

} // namespace anari_guard
