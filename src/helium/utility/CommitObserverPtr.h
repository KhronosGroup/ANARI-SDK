// Copyright 2022-2024 The Khronos Group
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
struct CommitObserverPtr
{
  CommitObserverPtr(BaseObject *observer, T *initialPointee = nullptr);
  ~CommitObserverPtr();

  CommitObserverPtr(CommitObserverPtr &&input);
  CommitObserverPtr &operator=(CommitObserverPtr &&input);

  CommitObserverPtr &operator=(T *input);

  operator bool() const;

  const T &operator*() const;
  T &operator*();

  const T *operator->() const;
  T *operator->();

  T *get() const;

  ////////////
  // Move-only
  CommitObserverPtr(const CommitObserverPtr &input) = delete;
  CommitObserverPtr &operator=(const CommitObserverPtr &input) = delete;
  ////////////

 private:
  void installObserver();
  void removeObserver();

  IntrusivePtr<T> ptr;
  BaseObject *observer{nullptr};
};

// Inlined definitions //

template <typename T>
inline CommitObserverPtr<T>::CommitObserverPtr(BaseObject *o, T *p)
    : observer(o), ptr(p)
{
  installObserver();
}

template <typename T>
inline CommitObserverPtr<T>::~CommitObserverPtr()
{
  removeObserver();
}

template <typename T>
inline CommitObserverPtr<T>::CommitObserverPtr(CommitObserverPtr<T> &&input)
    : ptr(input.ptr), observer(input.observer)
{
  input.ptr = nullptr;
  input.observer = nullptr;
}

template <typename T>
inline CommitObserverPtr<T> &CommitObserverPtr<T>::operator=(
    CommitObserverPtr &&input)
{
  ptr = input.ptr;
  observer = input.observer;

  input.ptr = nullptr;
  input.observer = nullptr;

  return *this;
}

template <typename T>
inline CommitObserverPtr<T> &CommitObserverPtr<T>::operator=(T *input)
{
  removeObserver();
  ptr = input;
  installObserver();
  return *this;
}

template <typename T>
inline CommitObserverPtr<T>::operator bool() const
{
  return ptr;
}

template <typename T>
inline const T &CommitObserverPtr<T>::operator*() const
{
  return *ptr;
}

template <typename T>
inline T &CommitObserverPtr<T>::operator*()
{
  return *ptr;
}

template <typename T>
inline const T *CommitObserverPtr<T>::operator->() const
{
  return get();
}

template <typename T>
inline T *CommitObserverPtr<T>::operator->()
{
  return get();
}

template <typename T>
inline T *CommitObserverPtr<T>::get() const
{
  return ptr.ptr;
}

template <typename T>
inline void CommitObserverPtr<T>::installObserver()
{
  if (observer && ptr)
    ptr->addCommitObserver(observer);
}

template <typename T>
inline void CommitObserverPtr<T>::removeObserver()
{
  if (observer && ptr)
    ptr->removeCommitObserver(observer);
}

// Inlined operators //////////////////////////////////////////////////////////

template <typename T>
inline bool operator<(
    const CommitObserverPtr<T> &a, const CommitObserverPtr<T> &b)
{
  return a.ptr.ptr < b.ptr.ptr;
}

template <typename T>
bool operator==(const CommitObserverPtr<T> &a, const CommitObserverPtr<T> &b)
{
  return a.ptr == b.ptr;
}

template <typename T>
bool operator!=(const CommitObserverPtr<T> &a, const CommitObserverPtr<T> &b)
{
  return a.ptr != b.ptr;
}

} // namespace helium
