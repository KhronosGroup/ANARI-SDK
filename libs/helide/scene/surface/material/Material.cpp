// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
// subtypes
#include "Matte.h"
#include "TransparentMatte.h"

namespace helide {

static size_t s_numMaterials = 0;

size_t Material::objectCount()
{
  return s_numMaterials;
}

Material::Material(HelideGlobalState *s) : Object(ANARI_MATERIAL, s)
{
  s_numMaterials++;
}

Material::~Material()
{
  s_numMaterials--;
}

Material *Material::createInstance(
    std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  else if (subtype == "transparentMatte")
    return new TransparentMatte(s);
  else
    return (Material *)new UnknownObject(ANARI_MATERIAL, s);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Material *);
