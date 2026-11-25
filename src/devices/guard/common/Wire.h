// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/Protocol.h"
#include "common/platform/Platform.h"

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace anari_guard {

using Buffer = std::vector<uint8_t>;

// Appends one message to a buffer; the header's size is filled in when the
// writer goes out of scope.
struct Writer
{
  Writer(Buffer &buffer, Op op, uint16_t descriptors = 0)
      : m_buffer(buffer), m_start(buffer.size())
  {
    MessageHeader h{0, uint16_t(op), descriptors};
    bytes(&h, sizeof(h));
  }

  ~Writer()
  {
    uint32_t size = uint32_t(m_buffer.size() - m_start - sizeof(MessageHeader));
    std::memcpy(m_buffer.data() + m_start, &size, sizeof(size));
  }

  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;

  template <typename T>
  void pod(const T &v)
  {
    static_assert(std::is_trivially_copyable<T>::value, "pod only");
    bytes(&v, sizeof(v));
  }

  void bytes(const void *data, size_t size)
  {
    auto *p = static_cast<const uint8_t *>(data);
    m_buffer.insert(m_buffer.end(), p, p + size);
  }

  // Room for 'size' bytes, to be filled in by the caller.
  uint8_t *reserve(size_t size)
  {
    size_t at = m_buffer.size();
    m_buffer.resize(at + size);
    return m_buffer.data() + at;
  }

  void string(const char *s)
  {
    if (!s) {
      pod(NULL_STRING);
      return;
    }
    string(s, std::strlen(s));
  }

  void string(const std::string &s)
  {
    string(s.data(), s.size());
  }

  void string(const char *s, size_t length)
  {
    pod(uint32_t(length));
    bytes(s, length);
  }

 private:
  Buffer &m_buffer;
  size_t m_start;
};

// Reads fields back out of a payload. Reading past the end clears ok() and
// yields zeros; callers check ok() once at the end.
struct Reader
{
  Reader(const uint8_t *data, size_t size) : m_p(data), m_end(data + size) {}
  explicit Reader(const Buffer &b) : Reader(b.data(), b.size()) {}

  bool ok() const
  {
    return m_ok;
  }

  size_t remaining() const
  {
    return size_t(m_end - m_p);
  }

  template <typename T>
  T pod()
  {
    T v{};
    if (auto *p = bytes(sizeof(T)))
      std::memcpy(&v, p, sizeof(T));
    return v;
  }

  const uint8_t *bytes(size_t size)
  {
    if (!m_ok || size > remaining()) {
      m_ok = false;
      return nullptr;
    }
    auto *p = m_p;
    m_p += size;
    return p;
  }

  // 'isNull' reports a null pointer; without it null reads as empty.
  std::string string(bool *isNull = nullptr)
  {
    uint32_t length = pod<uint32_t>();
    if (isNull)
      *isNull = length == NULL_STRING;
    if (length == NULL_STRING)
      return {};
    auto *p = bytes(length);
    return p ? std::string(reinterpret_cast<const char *>(p), length)
             : std::string();
  }

 private:
  const uint8_t *m_p;
  const uint8_t *m_end;
  bool m_ok{true};
};

// A string that may be a null pointer, as it came off the wire.
struct OptionalString
{
  explicit OptionalString(Reader &r)
  {
    value = r.string(&isNull);
  }

  const char *c_str() const
  {
    return isNull ? nullptr : value.c_str();
  }

  std::string value;
  bool isNull{false};
};

// Receive one whole message. False on end-of-file or failure.
inline bool recvMessage(int endpoint, MessageHeader &header, Buffer &payload)
{
  if (!platform::recvAll(endpoint, &header, sizeof(header)))
    return false;
  payload.resize(header.size);
  return header.size == 0
      || platform::recvAll(endpoint, payload.data(), header.size);
}

// As above, on a channel whose messages may carry a descriptor, which the
// caller then owns; -1 if this message had none. A message whose header and
// descriptor disagree is a failure.
inline bool recvMessage(
    int endpoint, MessageHeader &header, Buffer &payload, int &descriptor)
{
  // A descriptor travels with the first byte of its message, and no receive
  // here ever reads past the end of a message, so it can only turn up now.
  if (!platform::recvAll(endpoint, &header, sizeof(header), descriptor))
    return false;
  payload.resize(header.size);
  if ((header.descriptors != 0) != (descriptor >= 0) || header.descriptors > 1
      || (header.size != 0
          && !platform::recvAll(endpoint, payload.data(), header.size))) {
    platform::closeDescriptor(descriptor);
    descriptor = -1;
    return false;
  }
  return true;
}

} // namespace anari_guard
