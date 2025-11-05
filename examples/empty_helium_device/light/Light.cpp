// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"

namespace hecore {

Light::Light(HeCoreDeviceGlobalState *s) : Object(ANARI_LIGHT, s) {}

Light *Light::createInstance(
    std::string_view /*subtype*/, HeCoreDeviceGlobalState *s)
{
  return (Light *)new UnknownObject(ANARI_LIGHT, s);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Light *);
