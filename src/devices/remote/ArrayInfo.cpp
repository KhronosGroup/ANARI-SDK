// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ArrayInfo.h"
#include <anari/anari_cpp.hpp>
#include <cassert>

namespace remote {

ArrayInfo::ArrayInfo(ANARIDataType type, // 1D,2D,3D
    const ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
    : type(type),
      elementType(elementType),
      numItems1(numItems1),
      numItems2(numItems2),
      numItems3(numItems3)
{}

size_t ArrayInfo::getSizeInBytes() const
{
  if (type == ANARI_ARRAY1D) {
    return anari::sizeOf(elementType) * numItems1;
  } else if (type == ANARI_ARRAY2D) {
    return anari::sizeOf(elementType) * numItems1 * numItems2;
  } else if (type == ANARI_ARRAY3D) {
    return anari::sizeOf(elementType) * numItems1 * numItems2 * numItems3;
  }

  return 0;
}

} // namespace remote
