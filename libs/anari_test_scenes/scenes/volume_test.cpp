// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "volume_test.h"
// std
#include <random>

namespace anari {
namespace scenes {

struct Point
{
  glm::vec3 center;
  float weight;
};

static std::vector<Point> generatePoints(int numPoints)
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
            size_t(k) * dims.z * dims.y + size_t(j) * dims.x + size_t(i);

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

VolumeTest::VolumeTest(anari::Device d) : TestScene(d)
{
  m_world = anari::newObject<anari::World>(m_device);
}

VolumeTest::~VolumeTest()
{
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> VolumeTest::parameters()
{
  return {
      {"withGeometry", ANARI_BOOL, false, "Include geometry inside the volume?"},
      {"valueRangeLeft", ANARI_FLOAT32, 0.0f, "Sampled values of field are clamped to this range"},
      {"valueRangeRight", ANARI_FLOAT32, 1.0f, "Sampled values of field are clamped to this range"},
      {"densityScale", ANARI_FLOAT32, 1.0f, "Makes volumes uniformly thinner or thicker"}
      //
  };
}

anari::World VolumeTest::world()
{
  return m_world;
}

void VolumeTest::commit()
{
  anari::Device d = m_device;

  const bool withGeometry = getParam<bool>("withGeometry", false);
  float valueRangeLeft = getParam<float>("valueRangeLeft", 0.0f);
  float valueRangeRight = getParam<float>("valueRangeRight", 1.0f);
  float densityScale = getParam<float>("densityScale", 1.0f);
  const int volumeDims = 128;
  const int numPoints = 10;
  const auto voxelRange = glm::vec2(0.f, 10.f);

  float valueRange[2] = {valueRangeLeft, valueRangeRight};

  auto points = generatePoints(numPoints);
  auto voxels = generateVoxels(points, glm::ivec3(volumeDims));

  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameter(d, field, "origin", glm::vec3(-1.f));
  anari::setParameter(d, field, "spacing", glm::vec3(2.f / volumeDims));
  anari::setAndReleaseParameter(d, field, "data", anari::newArray3D(d, voxels.data(), volumeDims, volumeDims, volumeDims));
  anari::commit(d, field);

  auto volume = anari::newObject<anari::Volume>(d, "scivis");
  anari::setAndReleaseParameter(d, volume, "field", field);
  anari::setParameter(d, volume, "valueRange", valueRange);
  anari::setParameter(d, volume, "densityScale", densityScale);
  //anari::setParameter(d, volume, "color", field);
  //anari::setParameter(d, volume, "color.position", field);
  //anari::setParameter(d, volume, "opacity", field);

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
    anari::setParameter(d, volume, "valueRange", voxelRange);
  }

  anari::commit(d, volume);

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
    anari::commit(d, geom);

    auto mat = anari::newObject<anari::Material>(d, "matte");
    anari::commit(d, mat);

    auto surface = anari::newObject<anari::Surface>(d);
    anari::setAndReleaseParameter(d, surface, "geometry", geom);
    anari::setAndReleaseParameter(d, surface, "material", mat);
    anari::commit(d, surface);

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

  anari::commit(d, m_world);
}

TestScene *sceneVolumeTest(anari::Device d)
{
  return new VolumeTest(d);
}

} // namespace scenes
} // namespace anari