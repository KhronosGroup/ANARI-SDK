// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"

namespace hecore {

SpatialField::SpatialField(HeCoreDeviceGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{}

SpatialField *SpatialField::createInstance(
    std::string_view subtype, HeCoreDeviceGlobalState *s)
{
  return (SpatialField *)new UnknownObject(ANARI_SPATIAL_FIELD, s);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::SpatialField *);
