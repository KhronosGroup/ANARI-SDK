// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstring>
#include <string>
#include <vector>
// anari
#include <anari/anari.h>
// ours
#include "ObjectDesc.h"
#include "ParameterList.h"
#include "StringList.h"

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

  // read overload for object descriptor
  bool read(ObjectDesc &val);

  // write overload for object descriptor
  bool write(const ObjectDesc &val);

  // read overload for string lists
  bool read(StringList &val);

  // write overload for string lists
  bool write(const StringList &val);

  // read overload for parameter lists
  bool read(ParameterList &val);

  // write overload for parameter lists
  bool write(const ParameterList &val);

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
