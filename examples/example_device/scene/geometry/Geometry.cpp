// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// specific types
#include "Spheres.h"
#include "Triangles.h"

namespace anari {
namespace example_device {

static FactoryMapPtr<Geometry> g_geometries;

static void init()
{
  g_geometries = std::make_unique<FactoryMap<Geometry>>();

  g_geometries->emplace(
      "triangle", []() -> Geometry * { return new Triangles; });
  g_geometries->emplace("sphere", []() -> Geometry * { return new Spheres; });
}

Geometry::Geometry()
{
  setCommitPriority(COMMIT_PRIORITY_GEOMETRY);
}

Geometry *Geometry::createInstance(const char *type)
{
  if (g_geometries.get() == nullptr)
    init();

  auto *fcn = (*g_geometries)[type];

  if (fcn)
    return fcn();
  else {
    throw std::runtime_error("could not create geometry");
  }
}

void Geometry::commit()
{
  m_colorArray = getParamObject<Array1D>("primitive.color");
  m_color = m_colorArray ? m_colorArray->dataAs<vec4>() : nullptr;
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Geometry *);

} // namespace anari
