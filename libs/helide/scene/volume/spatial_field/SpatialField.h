// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct SpatialField : public Object
{
  SpatialField(HelideGlobalState *d);
  static SpatialField *createInstance(
      std::string_view subtype, HelideGlobalState *d);
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(
    helide::SpatialField *, ANARI_SPATIAL_FIELD);
