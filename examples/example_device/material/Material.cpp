// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
// specific types
#include "Matte.h"

namespace anari {
namespace example_device {

FactoryMapPtr<Material> Material::g_materials;

void Material::init()
{
  g_materials = std::make_unique<FactoryMap<Material>>();

  g_materials->emplace("matte", []() -> Material * { return new Matte; });
  g_materials->emplace(
      "transparentMatte", []() -> Material * { return new Matte; });
}

Material *Material::createInstance(const char *type)
{
  if (g_materials.get() == nullptr)
    init();

  auto *fcn = (*g_materials)[type];

  if (fcn)
    return fcn();
  else {
    throw std::runtime_error("could not create material");
  }
}

ANARIParameter *Material::parameters(const char *_type)
{
  std::string type(_type);
  if (type == "matte" || type == "transparentMatte") {
    return Matte::g_parameters;
  } else {
    return nullptr;
  }
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Material *);

} // namespace anari
