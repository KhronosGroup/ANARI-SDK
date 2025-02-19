// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../BaseObject.h"

namespace helium {

// Extend IntrusivePtr<> to automatically install/remove an observing object.
//
//   This smart pointer class builds on IntrusivePtr<> by having this pointer
//   also install/remove an observer automatically. The expected use of this
//   class is to pass the observing object's 'this' pointer as the 'observer'
//   parameter in the constructor, then all assignments to it will use that
//   object as the observer it manages on the incoming object being pointed to.
template <typename T = BaseObject>
struct ChangeObserverPtr
{
  ChangeObserverPtr(BaseObject *observer, T *initialPointee = nullptr);
  ~ChangeObserverPtr();

  ChangeObserverPtr(ChangeObserverPtr &&input);
  ChangeObserverPtr &operator=(ChangeObserverPtr &&input);

  ChangeObserverPtr &operator=(T *input);

  operator bool() const;

  const T &operator*() const;
  T &operator*();

  const T *operator->() const;
  T *operator->();

  T *get() const;

  ////////////
  // Move-only
  ChangeObserverPtr(const ChangeObserverPtr &input) = delete;
  ChangeObserverPtr &operator=(const ChangeObserverPtr &input) = delete;
  ////////////

 private:
  void installObserver();
  void removeObserver();

  IntrusivePtr<T> ptr;
  BaseObject *observer{nullptr};
};

// Inlined definitions //

template <typename T>
inline ChangeObserverPtr<T>::ChangeObserverPtr(BaseObject *o, T *p)
    : observer(o), ptr(p)
{
  installObserver();
}

template <typename T>
inline ChangeObserverPtr<T>::~ChangeObserverPtr()
{
  removeObserver();
}

template <typename T>
inline ChangeObserverPtr<T>::ChangeObserverPtr(ChangeObserverPtr<T> &&input)
    : ptr(input.ptr), observer(input.observer)
{
  input.ptr = nullptr;
  input.observer = nullptr;
}

template <typename T>
inline ChangeObserverPtr<T> &ChangeObserverPtr<T>::operator=(
    ChangeObserverPtr &&input)
{
  ptr = input.ptr;
  observer = input.observer;

  input.ptr = nullptr;
  input.observer = nullptr;

  return *this;
}

template <typename T>
inline ChangeObserverPtr<T> &ChangeObserverPtr<T>::operator=(T *input)
{
  removeObserver();
  ptr = input;
  installObserver();
  return *this;
}

template <typename T>
inline ChangeObserverPtr<T>::operator bool() const
{
  return ptr;
}

template <typename T>
inline const T &ChangeObserverPtr<T>::operator*() const
{
  return *ptr;
}

template <typename T>
inline T &ChangeObserverPtr<T>::operator*()
{
  return *ptr;
}

template <typename T>
inline const T *ChangeObserverPtr<T>::operator->() const
{
  return get();
}

template <typename T>
inline T *ChangeObserverPtr<T>::operator->()
{
  return get();
}

template <typename T>
inline T *ChangeObserverPtr<T>::get() const
{
  return ptr.ptr;
}

template <typename T>
inline void ChangeObserverPtr<T>::installObserver()
{
  if (observer && ptr)
    ptr->addChangeObserver(observer);
}

template <typename T>
inline void ChangeObserverPtr<T>::removeObserver()
{
  if (observer && ptr)
    ptr->removeChangeObserver(observer);
}

// Inlined operators //////////////////////////////////////////////////////////

template <typename T>
inline bool operator<(
    const ChangeObserverPtr<T> &a, const ChangeObserverPtr<T> &b)
{
  return a.ptr.ptr < b.ptr.ptr;
}

template <typename T>
bool operator==(const ChangeObserverPtr<T> &a, const ChangeObserverPtr<T> &b)
{
  return a.ptr == b.ptr;
}

template <typename T>
bool operator!=(const ChangeObserverPtr<T> &a, const ChangeObserverPtr<T> &b)
{
  return a.ptr != b.ptr;
}

} // namespace helium
