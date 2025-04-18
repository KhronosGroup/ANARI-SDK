// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <stdexcept>
#include <type_traits>

namespace helium {

enum RefType
{
  PUBLIC,
  STAGED, // when placed in AnariAny
  INTERNAL,
  ALL
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
  std::atomic<uint32_t> m_internalRefs{0};
  std::atomic<uint32_t> m_stagedRefs{0};
  std::atomic<uint32_t> m_publicRefs{1};
};

// Inlined definitions //

inline void RefCounted::refInc(RefType type)
{
  if (type == RefType::PUBLIC)
    m_publicRefs++;
  else if (type == RefType::STAGED)
    m_stagedRefs++;
  else if (type == RefType::INTERNAL)
    m_internalRefs++;
  else {
    m_publicRefs++;
    m_stagedRefs++;
    m_internalRefs++;
  }
}

inline void RefCounted::refDec(RefType type)
{
  const auto prevPublicRefs = useCount(RefType::PUBLIC);
  const auto prevStagedRefs = useCount(RefType::STAGED);
  const auto prevInternalRefs = useCount(RefType::INTERNAL);

  const bool publicRef = type == RefType::PUBLIC || type == RefType::ALL;
  const bool stagedRef = type == RefType::STAGED || type == RefType::ALL;
  const bool internalRef = type == RefType::INTERNAL || type == RefType::ALL;

  if (publicRef && useCount(RefType::PUBLIC) > 0)
    m_publicRefs--;
  if (stagedRef && useCount(RefType::STAGED) > 0)
    m_stagedRefs--;
  if (internalRef && useCount(RefType::INTERNAL) > 0)
    m_internalRefs--;

  if (useCount(RefType::ALL) == 0) {
    delete this;
    return;
  }

  if (publicRef && prevPublicRefs == 1)
    on_NoPublicReferences();
  if ((internalRef || stagedRef) && ((prevInternalRefs + prevStagedRefs) == 1))
    on_NoInternalReferences();
}

inline uint32_t RefCounted::useCount(RefType type) const
{
  if (type == RefType::PUBLIC)
    return m_publicRefs;
  else if (type == RefType::STAGED)
    return m_stagedRefs;
  else if (type == RefType::INTERNAL)
    return m_internalRefs;
  else
    return m_publicRefs + m_stagedRefs + m_internalRefs;
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
