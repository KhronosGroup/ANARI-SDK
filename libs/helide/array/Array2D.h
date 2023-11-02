// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../HelideGlobalState.h"
// helium
#include "helium/array/Array2D.h"

namespace helide {

using Array2DMemoryDescriptor = helium::Array2DMemoryDescriptor;

struct Array2D : public helium::Array2D
{
  Array2D(HelideGlobalState *state, const Array2DMemoryDescriptor &d);
  ~Array2D() override;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Array2D *, ANARI_ARRAY2D);
