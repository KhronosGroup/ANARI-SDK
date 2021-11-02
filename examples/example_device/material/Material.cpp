// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
// specific types
#include "OBJ.h"

namespace anari {
namespace example_device {

static FactoryMapPtr<Material> g_materials;

static void init()
{
  g_materials = std::make_unique<FactoryMap<Material>>();

  g_materials->emplace("obj", []() -> Material * { return new OBJ; });
  g_materials->emplace("matte", []() -> Material * { return new OBJ; });
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

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Material *);

} // namespace anari
