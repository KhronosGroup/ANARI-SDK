// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"

namespace hecore {

Material::Material(HeCoreDeviceGlobalState *s) : Object(ANARI_MATERIAL, s) {}

Material *Material::createInstance(
    std::string_view subtype, HeCoreDeviceGlobalState *s)
{
  return (Material *)new UnknownObject(ANARI_MATERIAL, s);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Material *);
