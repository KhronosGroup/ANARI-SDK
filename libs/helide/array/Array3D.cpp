// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "array/Array3D.h"

namespace helide {

Array3D::Array3D(HelideGlobalState *state, const Array3DMemoryDescriptor &d)
    : helium::Array3D(state, d)
{
  state->objectCounts.arrays++;
}

Array3D::~Array3D()
{
  asHelideState(deviceState())->objectCounts.arrays--;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Array3D *);
