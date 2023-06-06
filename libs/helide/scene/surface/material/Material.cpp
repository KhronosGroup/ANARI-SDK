// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
// subtypes
#include "Matte.h"
#include "PBM.h"

namespace helide {

Material::Material(HelideGlobalState *s) : Object(ANARI_MATERIAL, s)
{
  s->objectCounts.materials++;
}

Material::~Material()
{
  deviceState()->objectCounts.materials--;
}

Material *Material::createInstance(
    std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  else if (subtype == "physicallyBased")
    return new PBM(s);
  else
    return (Material *)new UnknownObject(ANARI_MATERIAL, s);
}

Attribute Material::attributeFromString(const std::string &str) const
{
  if (str == "color")
    return Attribute::COLOR;
  else if (str == "attribute0")
    return Attribute::ATTRIBUTE_0;
  else if (str == "attribute1")
    return Attribute::ATTRIBUTE_1;
  else if (str == "attribute2")
    return Attribute::ATTRIBUTE_2;
  else if (str == "attribute3")
    return Attribute::ATTRIBUTE_3;
  else
    return Attribute::NONE;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Material *);
