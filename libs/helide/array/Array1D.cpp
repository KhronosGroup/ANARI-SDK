// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array1D.h"

namespace helide {

Array1D::Array1D(HelideGlobalState *state, const Array1DMemoryDescriptor &d)
    : helium::Array1D(state, d)
{
  state->objectCounts.arrays++;
}

Array1D::~Array1D()
{
  asHelideState(deviceState())->objectCounts.arrays--;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array1D *);
