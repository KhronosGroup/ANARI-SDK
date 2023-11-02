// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../HelideGlobalState.h"
// helium
#include "helium/array/Array3D.h"

namespace helide {

using Array3DMemoryDescriptor = helium::Array3DMemoryDescriptor;

struct Array3D : public helium::Array3D
{
  Array3D(HelideGlobalState *state, const Array3DMemoryDescriptor &d);
  ~Array3D() override;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Array3D *, ANARI_ARRAY3D);
