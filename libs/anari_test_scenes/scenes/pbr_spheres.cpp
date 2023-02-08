// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "pbr_spheres.h"
// std
#include <random>

namespace anari {
namespace scenes {

PbrSpheres::PbrSpheres(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

PbrSpheres::~PbrSpheres()
{
  anari::release(m_device, m_world);
}

anari::World PbrSpheres::world()
{
  return m_world;
}

void PbrSpheres::commit()
{
  auto d = m_device;

  auto surface = anari::newObject<anari::Surface>(d);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  auto mat = anari::newObject<anari::Material>(d, "pbr");
  anari::setParameter(d, mat, "baseColor", glm::vec3(1,0,0));
  anari::setParameter(d, mat, "metallic", "attribute0");
  anari::setParameter(d, mat, "roughness", "attribute1");
  anari::commitParameters(d, mat);

  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));

  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", glm::vec3(0, 0, 1));
  anari::setParameter(d, light, "irradiance", 1.f);
  anari::commitParameters(d, light);
  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, &light));
  anari::release(d, light);

  anari::commitParameters(d, m_world);

  anari::setParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);

  std::vector<glm::vec3> spherePositions;
  std::vector<float> sphereMetallic;
  std::vector<float> sphereRoughness;
  for(int i = 0;i<10;++i) {
    for(int j = 0;j<10;++j) {
      spherePositions.push_back(glm::vec3(i, j, 0));
      sphereMetallic.push_back(i/10.0f + 0.1f);
      sphereRoughness.push_back(j/10.0f + 0.1f);
    }  
  }

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, spherePositions.data(), spherePositions.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute0",
      anari::newArray1D(d, sphereMetallic.data(), sphereMetallic.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.attribute1",
      anari::newArray1D(d, sphereRoughness.data(), sphereRoughness.size()));
  anari::setParameter(d, geom, "radius", 0.4f);

  anari::commitParameters(d, geom);
  anari::commitParameters(d, mat);
  anari::commitParameters(d, surface);

  // cleanup

  anari::release(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
}

TestScene *scenePbrSpheres(anari::Device d)
{
  return new PbrSpheres(d);
}

} // namespace scenes
} // namespace anari