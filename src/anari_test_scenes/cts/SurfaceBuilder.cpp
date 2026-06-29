// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SurfaceBuilder.h"

namespace anari {
namespace cts {

anari::Material makeMatteMaterial(anari::Device d, math::float3 color)
{
  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, material, "color", color);
  anari::commitParameters(d, material);
  return material;
}

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geometry, anari::Material material)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", geometry);
  anari::setParameter(d, surface, "material", material);
  anari::commitParameters(d, surface);
  return surface;
}

} // namespace cts
} // namespace anari
