// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace anari_ptc {

/*! sets a flag for the lifetime of the object and clears it again on
    destruction, whether the scope is left normally or by an exception */
class ScopedFlag
{
 public:
  explicit ScopedFlag(bool &flag);
  ~ScopedFlag();
  ScopedFlag(const ScopedFlag &) = delete;
  ScopedFlag &operator=(const ScopedFlag &) = delete;

 private:
  bool *m_flag{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline ScopedFlag::ScopedFlag(bool &flag) : m_flag(&flag)
{
  *m_flag = true;
}

inline ScopedFlag::~ScopedFlag()
{
  *m_flag = false;
}

} // namespace anari_ptc
