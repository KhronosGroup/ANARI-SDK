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

void Material::commit()
{
  m_alphaMode = alphaModeFromString(getParamString("alphaMode", "opaque"));
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Material *);
