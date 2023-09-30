// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Buffer.h"

namespace remote {

Buffer::Buffer(const char *ptr, size_t numBytes)
{
  write(ptr, numBytes);
  seek(0);
}

// read overload for std::string
bool Buffer::read(std::string &val)
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

// write overload for std::string
bool Buffer::write(const std::string &val)
{
  uint64_t strLen = val.size();
  if (!write(strLen))
    return false;
  size_t res = write(val.data(), val.size());
  return res == strLen;
}

// low-level read function
size_t Buffer::read(char *ptr, size_t numBytes)
{
  if (pos + numBytes > size())
    return 0;

  memcpy(ptr, data() + pos, numBytes);

  pos += numBytes;

  return numBytes;
}

// low-level write function
size_t Buffer::write(const char *ptr, size_t numBytes)
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
bool Buffer::eof() const
{
  return pos == size();
}

// jump to position
void Buffer::seek(size_t p)
{
  pos = p;
}

} // namespace remote
