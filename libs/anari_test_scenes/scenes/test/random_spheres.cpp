// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "random_spheres.h"
// std
#include <random>

namespace anari {
namespace scenes {

RandomSpheres::RandomSpheres(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

RandomSpheres::~RandomSpheres()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> RandomSpheres::parameters()
{
  return {
      // clang-format off
      {makeParameterInfo("numSpheres", "Number of spheres to generate", int(1e3), int(1), int(1e6))},
      {makeParameterInfo("radius", "Radius of all spheres", 1.5e-2f)},
      {makeParameterInfo("randomizeRadii", "Randomize per-sphere radius", true)}
      // clang-format on
  };
}

anari::World RandomSpheres::world()
{
  return m_world;
}

void RandomSpheres::commit()
{
  auto d = m_device;

  // Build this scene top-down to stress commit ordering guarantees

  setDefaultLight(m_world);

  auto surface = anari::newObject<anari::Surface>(d);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  anari::setParameterArray1D(d, m_world, "surface", &surface, 1);
  anari::commitParameters(d, m_world);

  anari::setParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);

  int numSpheres = getParam<int>("numSpheres", 2e4);
  float radius = getParam<float>("radius", 1.5e-2f);
  bool randomizeRadii = getParam<bool>("randomizeRadii", true);

  if (numSpheres < 1)
    throw std::runtime_error("'numSpheres' must be >= 1");

  if (radius <= 0.f)
    throw std::runtime_error("'radius' must be > 0.f");

  std::mt19937 rng;
  rng.seed(0);
  std::normal_distribution<float> vert_dist(0.5f, 0.5f);

  std::vector<anari::float3> spherePositions((size_t(numSpheres)));
  std::vector<anari::float4> sphereColors((size_t(numSpheres)));

  for (auto &s : spherePositions) {
    s.x = vert_dist(rng);
    s.y = vert_dist(rng);
    s.z = vert_dist(rng);
  }

  for (auto &s : sphereColors) {
    s.x = vert_dist(rng);
    s.y = vert_dist(rng);
    s.z = vert_dist(rng);
    s.w = 1.f;
  }

  anari::setParameterArray1D(d, geom, "vertex.position", spherePositions.data(), spherePositions.size());
  anari::setParameterArray1D(d, geom, "vertex.color", sphereColors.data(), sphereColors.size());

  if (randomizeRadii) {
    std::normal_distribution<float> radii_dist(radius / 10.f, radius);

    std::vector<float> sphereRadii((size_t(numSpheres)));
    for (auto &r : sphereRadii)
      r = std::fabs(radii_dist(rng));

    anari::setParameterArray1D(d, geom, "vertex.radius", sphereRadii.data(), sphereRadii.size());
  }

  anari::commitParameters(d, geom);
  anari::commitParameters(d, mat);
  anari::commitParameters(d, surface);

  // cleanup

  anari::release(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
}

TestScene *sceneRandomSpheres(anari::Device d)
{
  return new RandomSpheres(d);
}

} // namespace scenes
} // namespace anari