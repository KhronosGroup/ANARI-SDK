// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <mutex>

namespace helium {

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
