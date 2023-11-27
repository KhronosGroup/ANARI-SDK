// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/ObjectArray.h"
#include "../HelideGlobalState.h"

namespace helide {

ObjectArray::ObjectArray(
    HelideGlobalState *state, const Array1DMemoryDescriptor &d)
    : helium::ObjectArray(state, d)
{
  state->objectCounts.arrays++;
}

ObjectArray::~ObjectArray()
{
  asHelideState(deviceState())->objectCounts.arrays--;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::ObjectArray *);
