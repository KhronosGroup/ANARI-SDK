// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstring>
#include <string>
#include <vector>
// anari
#include <anari/anari.h>

namespace remote {

struct Buffer : std::vector<char>
{
  // Create empty buffer
  Buffer() = default;

  // Fill buffer with data and set position to zero
  // This is equivalent to creating an empty buffer
  // and then calling: write(ptr, numBytes); seek(0);
  Buffer(const char *ptr, size_t numBytes);

  // trivial or POD layout read function
  template <typename T>
  bool read(T &val)
  {
    size_t res = read((char *)&val, sizeof(val));
    return res == sizeof(val);
  }

  // trivial or POD layout write function
  template <typename T>
  bool write(const T &val)
  {
    size_t res = write((const char *)&val, sizeof(val));
    return res == sizeof(val);
  }

  // read overload for std::string
  bool read(std::string &val);

  // write overload for std::string
  bool write(const std::string &val);

  // low-level read function
  size_t read(char *ptr, size_t numBytes);

  // low-level read function
  size_t write(const char *ptr, size_t numBytes);

  // check if we moved past the end
  bool eof() const;

  // jump to position
  void seek(size_t p);

  size_t pos = 0;
};

} // namespace remote
