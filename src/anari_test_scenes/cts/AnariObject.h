// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include "anari/anari_cpp.hpp"
// std
#include <utility>

namespace anari {
namespace cts {

// Move-only ownership for a temporary ANARI object. Keeping the Device beside
// the object makes every return and exception path release through one small
// interface instead of hand-written cleanup blocks.
template <typename T>
class UniqueAnariObject
{
 public:
  UniqueAnariObject() = default;
  UniqueAnariObject(anari::Device device, T object)
      : m_device(device), m_object(object)
  {}

  ~UniqueAnariObject()
  {
    reset();
  }

  UniqueAnariObject(const UniqueAnariObject &) = delete;
  UniqueAnariObject &operator=(const UniqueAnariObject &) = delete;

  UniqueAnariObject(UniqueAnariObject &&other) noexcept
      : m_device(std::exchange(other.m_device, nullptr)),
        m_object(std::exchange(other.m_object, nullptr))
  {}

  UniqueAnariObject &operator=(UniqueAnariObject &&other) noexcept
  {
    if (this != &other) {
      reset();
      m_device = std::exchange(other.m_device, nullptr);
      m_object = std::exchange(other.m_object, nullptr);
    }
    return *this;
  }

  T get() const
  {
    return m_object;
  }

  explicit operator bool() const
  {
    return m_object != nullptr;
  }

  T release()
  {
    m_device = nullptr;
    return std::exchange(m_object, nullptr);
  }

  void reset()
  {
    if (m_object)
      anari::release(m_device, m_object);
    m_object = nullptr;
  }

 private:
  anari::Device m_device{nullptr};
  T m_object{nullptr};
};

} // namespace cts
} // namespace anari
