// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct Light : public Object
{
  Light(HelideGlobalState *d);
  static Light *createInstance(std::string_view subtype, HelideGlobalState *d);
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Light *, ANARI_LIGHT);
