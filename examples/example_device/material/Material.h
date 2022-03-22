// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace anari {
namespace example_device {

struct Material : public Object
{
  Material() = default;

  static FactoryMapPtr<Material> g_materials;
  static void init();
  static Material *createInstance(const char *type);

  virtual vec3 diffuse() const = 0;
};

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Material *, ANARI_MATERIAL);

} // namespace anari
