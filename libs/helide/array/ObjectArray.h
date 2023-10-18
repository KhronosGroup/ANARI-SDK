// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../HelideGlobalState.h"
#include "Array1D.h"
// helium
#include "helium/array/ObjectArray.h"

namespace helide {

struct ObjectArray : public helium::ObjectArray
{
  ObjectArray(HelideGlobalState *state, const Array1DMemoryDescriptor &d);
  ~ObjectArray() override;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::ObjectArray *, ANARI_ARRAY1D);
