// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct Volume : public Object
{
  Volume(HelideGlobalState *d);
  static Volume *createInstance(std::string_view subtype, HelideGlobalState *d);
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Volume *, ANARI_VOLUME);
