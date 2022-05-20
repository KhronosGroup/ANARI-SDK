// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <stdexcept>
#include <type_traits>

// anari
#include "anari/anari.h"

namespace anari {

enum RefType
{
  PUBLIC,
  INTERNAL,
  ALL
};

class ANARI_INTERFACE RefCounted
{
 public:
  RefCounted() = default;
  virtual ~RefCounted() = default;

  RefCounted(const RefCounted &) = delete;
  RefCounted(RefCounted &&) = delete;

  RefCounted &operator=(const RefCounted &) = delete;
  RefCounted &operator=(RefCounted &&) = delete;

  void refInc(RefType = PUBLIC) const;
  void refDec(RefType = PUBLIC) const;
  uint64_t useCount(RefType = ALL) const;

 private:
  uint64_t internalRefCount() const;

  static constexpr uint64_t m_publicRefMask = 0x00000000FFFFFFFF;
  static constexpr uint64_t m_internalRefMask = 0xFFFFFFFF00000000;

  mutable std::atomic<uint64_t> m_refCounter{1};
};

// Inlined definitions //

inline void RefCounted::refInc(RefType type) const
{
  if (type == RefType::PUBLIC)
    m_refCounter++;
  else if (type == RefType::INTERNAL)
    m_refCounter = ((internalRefCount() + 1) << 32) + useCount(RefType::PUBLIC);
}

inline void RefCounted::refDec(RefType type) const
{
  if (type == RefType::PUBLIC && useCount(RefType::PUBLIC) > 0)
    m_refCounter--;
  else if (type == RefType::INTERNAL && internalRefCount() > 0)
    m_refCounter = ((internalRefCount() - 1) << 32) + useCount(RefType::PUBLIC);

  if (useCount(RefType::ALL) == 0)
    delete this;
}

inline uint64_t RefCounted::useCount(RefType type) const
{
  auto publicCount = m_refCounter.load() & m_publicRefMask;
  auto internalCount = internalRefCount();

  if (type == RefType::PUBLIC)
    return publicCount;
  else if (type == RefType::INTERNAL)
    return internalCount;
  else
    return publicCount + internalCount;
}

inline uint64_t RefCounted::internalRefCount() const
{
  return (m_refCounter.load() & m_internalRefMask) >> 32;
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

} // namespace anari
