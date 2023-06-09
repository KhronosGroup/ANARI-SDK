// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstring>
#include <vector>

namespace remote {

struct Buffer : std::vector<char>
{
  size_t read(char *ptr, size_t numBytes)
  {
    if (pos + numBytes > size())
      return 0;

    memcpy(ptr, data() + pos, numBytes);

    pos += numBytes;

    return numBytes;
  }

  size_t write(const char *ptr, size_t numBytes)
  {
    if (pos + numBytes >= size()) {
      try {
        resize(pos + numBytes);
      } catch (...) {
        return 0;
      }
    }

    memcpy(data() + pos, ptr, numBytes);

    pos += numBytes;

    return numBytes;
  }

  bool eof() const
  {
    return pos == size();
  }

  void seek(size_t p)
  {
    pos = p;
  }

  size_t pos = 0;
};

} // namespace remote
