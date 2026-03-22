// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <mutex>

namespace helium {

/*
 * Mixin that provides per-object mutual exclusion. BaseObject and BaseDevice
 * both inherit this so that concurrent API calls operating on the same object
 * can be serialized with scopeLockObject(). The lock is acquired through RAII
 * via std::scoped_lock and released automatically when the returned value goes
 * out of scope.
 */
struct LockableObject
{
  LockableObject() = default;
  virtual ~LockableObject() = default;

  std::scoped_lock<std::mutex> scopeLockObject();

 private:
  std::mutex m_objectMutex;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline std::scoped_lock<std::mutex> LockableObject::scopeLockObject()
{
  return std::scoped_lock<std::mutex>(m_objectMutex);
}

} // namespace helium
