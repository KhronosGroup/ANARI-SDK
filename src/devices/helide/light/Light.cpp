// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Light.h"

namespace helide {

Light::Light(HelideGlobalState *s) : Object(ANARI_LIGHT, s) {}

Light *Light::createInstance(std::string_view /*subtype*/, HelideGlobalState *s)
{
  return (Light *)new UnknownObject(ANARI_LIGHT, s);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Light *);
