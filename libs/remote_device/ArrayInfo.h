// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari.h>
#include "Buffer.h"

namespace remote {

struct ArrayInfo
{
  ArrayInfo() = default;
  ArrayInfo(ANARIDataType type, // 1D,2D,3D
      const ANARIDataType elementType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3);

  ANARIDataType type;
  ANARIDataType elementType;
  uint64_t numItems1, numItems2, numItems3;

  size_t getSizeInBytes() const;
};

} // namespace remote
