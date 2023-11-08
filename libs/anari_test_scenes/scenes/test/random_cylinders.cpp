// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "random_cylinders.h"
// std
#include <random>

namespace anari {
namespace scenes {

RandomCylinders::RandomCylinders(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

RandomCylinders::~RandomCylinders()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> RandomCylinders::parameters()
{
  return {
      // clang-format off
      {makeParameterInfo("numCylinders", "Number of cylinders to generate", int(1e3))},
      {makeParameterInfo("radius", "Radius of all cylinders", 1.5e-2f)}
      // clang-format on
  };
}

anari::World RandomCylinders::world()
{
  return m_world;
}

void RandomCylinders::commit()
{
  auto d = m_device;

  // Build this scene top-down to stress commit ordering guarantees

  setDefaultLight(m_world);

  auto surface = anari::newObject<anari::Surface>(d);
  auto geom = anari::newObject<anari::Geometry>(d, "cylinder");
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));

  anari::commitParameters(d, m_world);

  anari::setParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);

  int numCylinders = getParam<int>("numCylinders", 2e3);
  float radius = getParam<float>("radius", 1.5e-2f);
  bool randomizeRadii = getParam<bool>("randomizeRadii", true);

  if (numCylinders < 1)
    throw std::runtime_error("'numCylinders' must be >= 1");

  if (radius <= 0.f)
    throw std::runtime_error("'radius' must be > 0.f");

  std::mt19937 rng;
  rng.seed(0);
  std::normal_distribution<float> vert_dist(0.5f, 0.5f);

  std::vector<math::float3> cylinderPositions(2 * (size_t(numCylinders)));
  std::vector<math::float4> cylinderColors(2 * (size_t(numCylinders)));

  for (int i = 0; i < numCylinders; ++i) {
    auto &a = cylinderPositions[2 * i];
    a.x = vert_dist(rng);
    a.y = vert_dist(rng);
    a.z = vert_dist(rng);

    auto &b = cylinderPositions[2 * i + 1];
    b.x = a.x + 0.1 * vert_dist(rng);
    b.y = a.y + 0.1 * vert_dist(rng);
    b.z = a.z + 0.1 * vert_dist(rng);
  }

  for (auto &s : cylinderColors) {
    s.x = vert_dist(rng);
    s.y = vert_dist(rng);
    s.z = vert_dist(rng);
    s.w = 1.f;
  }

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, cylinderPositions.data(), cylinderPositions.size()));
  anari::setParameter(d, geom, "radius", radius);
  anari::setParameter(d, geom, "caps", "none");

  anari::setAndReleaseParameter(d,
      geom,
      "vertex.color",
      anari::newArray1D(d, cylinderColors.data(), cylinderColors.size()));

  if (randomizeRadii) {
    std::normal_distribution<float> radii_dist(radius / 10.f, radius);

    std::vector<float> cylinderRadii((size_t(numCylinders)));
    for (auto &r : cylinderRadii)
      r = std::abs(radii_dist(rng));

    anari::setAndReleaseParameter(d,
        geom,
        "primitive.radius",
        anari::newArray1D(d, cylinderRadii.data(), cylinderRadii.size()));
  }

  anari::commitParameters(d, geom);
  anari::commitParameters(d, mat);
  anari::commitParameters(d, surface);

  // cleanup

  anari::release(d, surface);
  anari::release(d, geom);
  anari::release(d, mat);
}

TestScene *sceneRandomCylinders(anari::Device d)
{
  return new RandomCylinders(d);
}

} // namespace scenes
} // namespace anari