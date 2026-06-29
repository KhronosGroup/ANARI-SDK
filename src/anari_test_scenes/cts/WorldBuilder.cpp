// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "WorldBuilder.h"

namespace anari {
namespace cts {

anari::World assembleWorld(anari::Device d, const WorldContents &contents)
{
  auto world = anari::newObject<anari::World>(d);
  if (!contents.surfaces.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "surface",
        anari::newArray1D(
            d, contents.surfaces.data(), contents.surfaces.size()));
  }
  if (!contents.volumes.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "volume",
        anari::newArray1D(d, contents.volumes.data(), contents.volumes.size()));
  }
  if (!contents.lights.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "light",
        anari::newArray1D(d, contents.lights.data(), contents.lights.size()));
  }
  if (!contents.instances.empty()) {
    anari::setAndReleaseParameter(d,
        world,
        "instance",
        anari::newArray1D(
            d, contents.instances.data(), contents.instances.size()));
  }
  anari::commitParameters(d, world);
  return world;
}

scenes::Bounds worldBounds(anari::Device d, anari::World world)
{
  scenes::Bounds bounds = {math::float3(-1.f), math::float3(1.f)};
  anariGetProperty(d,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      &bounds,
      sizeof(bounds),
      ANARI_WAIT);
  return bounds;
}

} // namespace cts
} // namespace anari
