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

  // read std::string
  bool read(std::string &val)
  {
    uint64_t strLen = 0;
    read((char *)&strLen, sizeof(strLen));
    std::vector<char> bytesToString(strLen);
    size_t res = read(bytesToString.data(), bytesToString.size());
    if (res == strLen) {
      val = std::string(bytesToString.data(), bytesToString.size());
      return true;
    } else {
      return false;
    }
  }

  // write std::string
  bool write(const std::string &val)
  {
    uint64_t strLen = val.size();
    if (!write(strLen))
      return false;
    size_t res = write(val.data(), val.size());
    return res == strLen;
  }

  // low-level read function
  size_t read(char *ptr, size_t numBytes)
  {
    if (pos + numBytes > size())
      return 0;

    memcpy(ptr, data() + pos, numBytes);

    pos += numBytes;

    return numBytes;
  }

  // low-level read function
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

  // check if we moved past the end
  bool eof() const
  {
    return pos == size();
  }

  // jump to position
  void seek(size_t p)
  {
    pos = p;
  }

  size_t pos = 0;
};

} // namespace remote
