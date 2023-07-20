// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseArray.h"
// std
#include <algorithm>

namespace helium {

BaseArray::BaseArray(ANARIDataType type, BaseGlobalDeviceState *s)
    : BaseObject(type, s)
{}

} // namespace helium

HELIUM_ANARI_TYPEFOR_DEFINITION(helium::BaseArray *);
