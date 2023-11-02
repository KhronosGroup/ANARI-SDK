// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array2D.h"
#include "../HelideGlobalState.h"

namespace helide {

Array2D::Array2D(HelideGlobalState *state, const Array2DMemoryDescriptor &d)
    : helium::Array2D(state, d)
{
  state->objectCounts.arrays++;
}

Array2D::~Array2D()
{
  asHelideState(deviceState())->objectCounts.arrays--;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array2D *);
