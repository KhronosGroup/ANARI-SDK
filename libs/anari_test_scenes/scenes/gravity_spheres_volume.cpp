// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "gravity_spheres_volume.h"
// std
#include <random>

namespace anari {
namespace scenes {

struct Point
{
  glm::vec3 center;
  float weight;
};

static std::vector<Point> generatePoints(size_t numPoints)
{
  // create random number distributions for point center and weight
  std::mt19937 gen(0);

  std::uniform_real_distribution<float> centerDistribution(-1.f, 1.f);
  std::uniform_real_distribution<float> weightDistribution(0.1f, 0.3f);

  // populate the points
  std::vector<Point> points(numPoints);

  for (auto &p : points) {
    p.center.x = centerDistribution(gen);
    p.center.y = centerDistribution(gen);
    p.center.z = centerDistribution(gen);

    p.weight = weightDistribution(gen);
  }

  return points;
}

static std::vector<float> generateVoxels(
    const std::vector<Point> &points, glm::ivec3 dims)
{
  // get world coordinate in [-1.f, 1.f] from logical coordinates in [0,
  // volumeDimension)
  auto logicalToWorldCoordinates = [&](int i, int j, int k) {
    return glm::vec3(-1.f + float(i) / float(dims.x - 1) * 2.f,
        -1.f + float(j) / float(dims.y - 1) * 2.f,
        -1.f + float(k) / float(dims.z - 1) * 2.f);
  };

  // generate voxels
  std::vector<float> voxels(size_t(dims.x) * size_t(dims.y) * size_t(dims.z));

  for (int k = 0; k < dims.z; k++) {
    for (int j = 0; j < dims.y; j++) {
      for (int i = 0; i < dims.x; i++) {
        // index in array
        size_t index =
            size_t(k) * size_t(dims.z) * size_t(dims.y) + size_t(j) * size_t(dims.x) + size_t(i);

        // compute volume value
        float value = 0.f;

        for (auto &p : points) {
          glm::vec3 pointCoordinate = logicalToWorldCoordinates(i, j, k);
          const float distance = glm::length(pointCoordinate - p.center);

          // contribution proportional to weighted inverse-square distance
          // (i.e. gravity)
          value += p.weight / (distance * distance);
        }

        voxels[index] = value;
      }
    }
  }

  return voxels;
}

GravityVolume::GravityVolume(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

GravityVolume::~GravityVolume()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> GravityVolume::parameters()
{
  return {
      {"withGeometry", ANARI_BOOL, false, "Include geometry inside the volume?"}
      //
  };
}

anari::World GravityVolume::world()
{
  return m_world;
}

void GravityVolume::commit()
{
  anari::Device d = m_device;

  const bool withGeometry = getParam<bool>("withGeometry", false);
  const int volumeDims = 128;
  const size_t numPoints = 10;
  const float voxelRange[2] = {0.f, 10.f};

  auto points = generatePoints(numPoints);
  auto voxels = generateVoxels(points, glm::ivec3(volumeDims));

  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameter(d, field, "origin", glm::vec3(-1.f));
  anari::setParameter(d, field, "spacing", glm::vec3(2.f / volumeDims));
  anari::setAndReleaseParameter(d,
      field,
      "data",
      anari::newArray3D(d, voxels.data(), volumeDims, volumeDims, volumeDims));
  anari::commitParameters(d, field);

  auto volume = anari::newObject<anari::Volume>(d, "scivis");
  anari::setAndReleaseParameter(d, volume, "field", field);

  {
    std::vector<glm::vec3> colors;
    std::vector<float> opacities;

    colors.emplace_back(0.f, 0.f, 1.f);
    colors.emplace_back(0.f, 1.f, 0.f);
    colors.emplace_back(1.f, 0.f, 0.f);

    opacities.emplace_back(0.f);
    opacities.emplace_back(1.f);

    anari::setAndReleaseParameter(
        d, volume, "color", anari::newArray1D(d, colors.data(), colors.size()));
    anari::setAndReleaseParameter(d,
        volume,
        "opacity",
        anari::newArray1D(d, opacities.data(), opacities.size()));
    anariSetParameter(d, volume, "valueRange", ANARI_FLOAT32_BOX1, voxelRange);
  }

  anari::commitParameters(d, volume);

  if (withGeometry) {
    std::vector<glm::vec3> positions(numPoints);
    std::transform(
        points.begin(), points.end(), positions.begin(), [](const Point &p) {
          return p.center;
        });

    ANARIGeometry geom = anari::newObject<anari::Geometry>(d, "sphere");
    anari::setAndReleaseParameter(d,
        geom,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
    anari::setParameter(d, geom, "radius", 0.05f);
    anari::commitParameters(d, geom);

    auto mat = anari::newObject<anari::Material>(d, "matte");
    anari::commitParameters(d, mat);

    auto surface = anari::newObject<anari::Surface>(d);
    anari::setAndReleaseParameter(d, surface, "geometry", geom);
    anari::setAndReleaseParameter(d, surface, "material", mat);
    anari::commitParameters(d, surface);

    anari::setAndReleaseParameter(
        d, m_world, "surface", anari::newArray1D(d, &surface));
    anari::release(d, surface);
  } else {
    anari::unsetParameter(d, m_world, "surface");
  }

  anari::setAndReleaseParameter(
      d, m_world, "volume", anari::newArray1D(d, &volume));
  anari::release(d, volume);

  setDefaultLight(m_world);

  anari::commitParameters(d, m_world);
}

TestScene *sceneGravitySphereVolume(anari::Device d)
{
  return new GravityVolume(d);
}

} // namespace scenes
} // namespace anari