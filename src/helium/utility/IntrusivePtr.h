// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <stdexcept>
#include <type_traits>
#include <cstdint>
#include <cassert>

namespace helium {

enum RefType : std::uint64_t
{
  PUBLIC = 1,
  INTERNAL = UINT64_C(0x100000000),
  ALL = PUBLIC+INTERNAL
};

class RefCounted
{
 public:
  RefCounted() = default;
  virtual ~RefCounted() = default;

  RefCounted(const RefCounted &) = delete;
  RefCounted(RefCounted &&) = delete;

  RefCounted &operator=(const RefCounted &) = delete;
  RefCounted &operator=(RefCounted &&) = delete;

  void refInc(RefType = RefType::INTERNAL);
  void refDec(RefType = RefType::INTERNAL);
  uint32_t useCount(RefType = ALL) const;

 protected:
  virtual void on_NoPublicReferences();
  virtual void on_NoInternalReferences();

 private:
  static constexpr std::uint64_t PUBLIC_MASK = UINT64_C(0x00000000FFFFFFFF);
  static constexpr std::uint64_t INTERNAL_MASK = UINT64_C(0xFFFFFFFF00000000);
  std::atomic<std::uint64_t> m_count{1};
};

// Inlined definitions //

inline void RefCounted::refInc(RefType type)
{
  assert(type == PUBLIC || type == INTERNAL);

  m_count += type;
}

inline void RefCounted::refDec(RefType type)
{
  assert(type == PUBLIC || type == INTERNAL);

  std::uint64_t prev = m_count.fetch_sub(type);
  // if the previous value was type it has to be 0 now
  if (prev == type) {
    delete this;
    return;
  }

  if (type == PUBLIC && (prev&PUBLIC_MASK) == PUBLIC)
    on_NoPublicReferences();
  if (type == INTERNAL && (prev&INTERNAL_MASK) == INTERNAL)
    on_NoInternalReferences();
}

inline uint32_t RefCounted::useCount(RefType type) const
{
  if (type == RefType::PUBLIC)
    return m_count&PUBLIC_MASK;
  else if (type == RefType::INTERNAL)
    return m_count>>UINT64_C(32);
  else
    return (m_count&PUBLIC_MASK) + (m_count>>UINT64_C(32));
}

inline void RefCounted::on_NoPublicReferences()
{
  // no-op
}

inline void RefCounted::on_NoInternalReferences()
{
  // no-op
}

///////////////////////////////////////////////////////////////////////////////
// Pointer to a RefCounted object /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template <typename T = RefCounted>
class IntrusivePtr
{
  static_assert(std::is_base_of<RefCounted, T>::value,
      "IntrusivePtr<T> can only be used with objects derived "
      "from RefCounted");

 public:
  T *ptr{nullptr};

  IntrusivePtr() = default;
  ~IntrusivePtr();

  IntrusivePtr(const IntrusivePtr &input);
  IntrusivePtr(IntrusivePtr &&input);

  template <typename O>
  IntrusivePtr(const IntrusivePtr<O> &input);
  IntrusivePtr(T *const input);

  IntrusivePtr &operator=(const IntrusivePtr &input);
  IntrusivePtr &operator=(IntrusivePtr &&input);
  IntrusivePtr &operator=(T *input);

  operator bool() const;

  const T &operator*() const;
  T &operator*();

  const T *operator->() const;
  T *operator->();
};

// Inlined definitions //

template <typename T>
inline IntrusivePtr<T>::~IntrusivePtr()
{
  if (ptr)
    ptr->refDec(RefType::INTERNAL);
}

template <typename T>
inline IntrusivePtr<T>::IntrusivePtr(const IntrusivePtr<T> &input)
    : ptr(input.ptr)
{
  if (ptr)
    ptr->refInc(RefType::INTERNAL);
}

template <typename T>
inline IntrusivePtr<T>::IntrusivePtr(IntrusivePtr<T> &&input) : ptr(input.ptr)
{
  input.ptr = nullptr;
}

template <typename T>
template <typename O>
inline IntrusivePtr<T>::IntrusivePtr(const IntrusivePtr<O> &input)
    : ptr(input.ptr)
{
  if (ptr)
    ptr->refInc(RefType::INTERNAL);
}

template <typename T>
inline IntrusivePtr<T>::IntrusivePtr(T *const input) : ptr(input)
{
  if (ptr)
    ptr->refInc(RefType::INTERNAL);
}

template <typename T>
inline IntrusivePtr<T> &IntrusivePtr<T>::operator=(const IntrusivePtr &input)
{
  if (input.ptr)
    input.ptr->refInc(RefType::INTERNAL);
  if (ptr)
    ptr->refDec(RefType::INTERNAL);
  ptr = input.ptr;
  return *this;
}

template <typename T>
inline IntrusivePtr<T> &IntrusivePtr<T>::operator=(IntrusivePtr &&input)
{
  if (ptr)
    ptr->refDec();
  ptr = input.ptr;
  input.ptr = nullptr;
  return *this;
}

template <typename T>
inline IntrusivePtr<T> &IntrusivePtr<T>::operator=(T *input)
{
  if (input)
    input->refInc(RefType::INTERNAL);
  if (ptr)
    ptr->refDec(RefType::INTERNAL);
  ptr = input;
  return *this;
}

template <typename T>
inline IntrusivePtr<T>::operator bool() const
{
  return ptr != nullptr;
}

template <typename T>
inline const T &IntrusivePtr<T>::operator*() const
{
  return *ptr;
}

template <typename T>
inline T &IntrusivePtr<T>::operator*()
{
  return *ptr;
}

template <typename T>
inline const T *IntrusivePtr<T>::operator->() const
{
  return ptr;
}

template <typename T>
inline T *IntrusivePtr<T>::operator->()
{
  return ptr;
}

// Inlined operators //////////////////////////////////////////////////////////

template <typename T>
inline bool operator<(const IntrusivePtr<T> &a, const IntrusivePtr<T> &b)
{
  return a.ptr < b.ptr;
}

template <typename T>
bool operator==(const IntrusivePtr<T> &a, const IntrusivePtr<T> &b)
{
  return a.ptr == b.ptr;
}

template <typename T>
bool operator!=(const IntrusivePtr<T> &a, const IntrusivePtr<T> &b)
{
  return a.ptr != b.ptr;
}

} // namespace helium
