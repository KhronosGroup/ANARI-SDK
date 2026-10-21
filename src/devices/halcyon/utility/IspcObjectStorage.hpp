// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace halcyon {

template <int SIZE = 256>
struct alignas(16) IspcObjectStorage
{
  char data[SIZE];

  operator void *() const
  {
    return (void *)data;
  }

  constexpr int size() const
  {
    return SIZE;
  }
};

} // namespace halcyon
