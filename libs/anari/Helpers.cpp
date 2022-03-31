// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/detail/Helpers.h"
#include "anari/type_utility.h"
// std
#include <cstdint>
#include <stdexcept>

namespace anari {

size_t sizeOfDataType(ANARIDataType type)
{
  return anari_sizeof(type);
}

} // namespace anari
