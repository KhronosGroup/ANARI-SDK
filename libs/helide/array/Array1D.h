// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../HelideGlobalState.h"
// helium
#include "helium/array/Array1D.h"

namespace helide {

using Array1DMemoryDescriptor = helium::Array1DMemoryDescriptor;

struct Array1D : public helium::Array1D
{
  Array1D(HelideGlobalState *state, const Array1DMemoryDescriptor &d);
  ~Array1D() override;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Array1D *, ANARI_ARRAY1D);
