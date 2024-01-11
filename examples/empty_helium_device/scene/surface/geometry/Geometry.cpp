// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"

namespace hecore {

Geometry::Geometry(HeCoreDeviceGlobalState *s) : Object(ANARI_GEOMETRY, s) {}

Geometry *Geometry::createInstance(
    std::string_view subtype, HeCoreDeviceGlobalState *s)
{
  return (Geometry *)new UnknownObject(ANARI_GEOMETRY, s);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Geometry *);
