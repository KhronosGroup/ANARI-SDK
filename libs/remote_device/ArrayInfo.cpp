// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ArrayInfo.h"
#include <anari/anari_cpp.hpp>
#include <cassert>

namespace remote {

ArrayInfo::ArrayInfo(ANARIDataType type, // 1D,2D,3D
    const ANARIDataType elementType,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3,
    uint64_t byteStride1,
    uint64_t byteStride2,
    uint64_t byteStride3)
    : type(type),
      elementType(elementType),
      numItems1(numItems1),
      numItems2(numItems2),
      numItems3(numItems3),
      byteStride1(byteStride1),
      byteStride2(byteStride2),
      byteStride3(byteStride3)
{}

size_t ArrayInfo::getSizeInBytes() const
{
  if (type == ANARI_ARRAY1D) {
    assert(byteStride1 <= 1);
    return anari::sizeOf(elementType) * numItems1;
  } else if (type == ANARI_ARRAY2D) {
    assert(byteStride1 <= 1 && byteStride2 <= 1);
    return anari::sizeOf(elementType) * numItems1 * numItems2;
  } else if (type == ANARI_ARRAY3D) {
    assert(byteStride1 <= 1 && byteStride2 <= 1 && byteStride3 <= 1);
    return anari::sizeOf(elementType) * numItems1 * numItems2 * numItems3;
  }

  return 0;
}

} // namespace remote
