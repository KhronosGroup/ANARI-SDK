// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "primitives.h"
#include "anari_test_scenes/generators/PrimitiveGenerator.h"

namespace {
constexpr std::uint32_t defaultNumObjects = 1024;
const char *defaultGeometrySubtype = "cone";
constexpr bool defaultCapping = false;
std::vector<std::string> geometrySubtypes(
    {"cone", "curve", "cylinder", "quad", "sphere", "triangle"});
} // namespace

namespace anari {
namespace scenes {
Primitives::Primitives(anari::Device d)
    : TestScene(d), m_world(anari::newObject<anari::World>(m_device))
{}

Primitives::~Primitives()
{
  if (m_geometry) {
    anari::release(m_device, m_geometry);
  }
  if (m_material) {
    anari::release(m_device, m_material);
  }
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> Primitives::parameters()
{
  return {
      // clang-format off
      // param, description, default, value, min, max
      {makeParameterInfo("geometry", "Geometry", defaultGeometrySubtype, geometrySubtypes)},
      {makeParameterInfo("numObjects", "Number of objects", defaultNumObjects, 8u, 1u << 20)},
      {makeParameterInfo("enableCapping", "Capping", defaultCapping)},
      // clang-format on
  };
}

anari::World Primitives::world()
{
  return m_world;
}

void Primitives::commit()
{
  auto &d = m_device;

  // Generate lights
  std::array<math::float3, 6> directions;
  directions[0] = {-1, 0, 0};
  directions[1] = {1, 0, 0};
  directions[2] = {0, -1, 0};
  directions[3] = {0, 1, 0};
  directions[4] = {0, 0, 0 - 1};
  directions[5] = {0, 0, 1};
  std::vector<anari::Light> lights;
  for (int i = 0; i < 6; ++i) {
    auto light = anari::newObject<anari::Light>(m_device, "directional");
    anari::setParameter(m_device, light, "direction", directions[i]);
    anari::setParameter(m_device, light, "irradiance", 1.f);
    anari::commitParameters(m_device, light);
    lights.emplace_back(light);
  }
  anari::setAndReleaseParameter(m_device,
      m_world,
      "light",
      anari::newArray1D(m_device, lights.data(), lights.size()));
  for (auto &l : lights) {
    anari::release(m_device, l);
  }

  auto surface = anari::newObject<anari::Surface>(d);
  this->generateGeometry();
  anari::setParameter(d, surface, "geometry", m_geometry);
  this->generateMaterial();
  anari::setParameter(d, surface, "material", m_material);
  anari::commitParameters(d, surface);
  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));
  anari::release(d, surface);

  anari::commitParameters(d, m_world);
}

anari::Geometry Primitives::generateGeometry(anari::Device d,
    std::uint32_t numObjects,
    const char *subtype,
    bool enableCapping)
{
  auto geometry = anari::newObject<anari::Geometry>(d, subtype);

  anari::scenes::PrimitiveGenerator generator(0);
  if (!strcmp(subtype, "cone")) {
    auto [positions, radii, caps] =
        generator.generateCones(numObjects, enableCapping ? 1 : 0);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.cap",
        anari::newArray1D(d, caps.data(), caps.size()));
  } else if (!strcmp(subtype, "curve")) {
    auto [positions, radii] = generator.generateCurves(numObjects);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
  } else if (!strcmp(subtype, "cylinder")) {
    auto [positions, radii, caps] =
        generator.generateCylinders(numObjects, enableCapping ? 1 : 0);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.cap",
        anari::newArray1D(d, caps.data(), caps.size()));
  } else if (!strcmp(subtype, "quad")) {
    auto positions = generator.generateQuads(numObjects);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
  } else if (!strcmp(subtype, "sphere")) {
    auto [positions, radii] = generator.generateSpheres(numObjects);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.radius",
        anari::newArray1D(d, radii.data(), radii.size()));
  } else if (!strcmp(subtype, "triangle")) {
    auto positions = generator.generateTriangles(numObjects);
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, positions.data(), positions.size()));
  }

  anari::commitParameters(d, geometry);
  return geometry;
}

void Primitives::generateGeometry()
{
  auto &d = m_device;
  const int numObjects =
      getParam<std::uint32_t>("numObjects", defaultNumObjects);
  const auto geometrySubType =
      getParamString("geometry", defaultGeometrySubtype);
  const bool enableCapping = getParam<bool>("enableCapping", defaultCapping);

  if (m_geometry) {
    anari::release(d, m_geometry);
  }
  m_geometry =
      generateGeometry(d, numObjects, geometrySubType.c_str(), enableCapping);
}

void Primitives::generateMaterial()
{
  auto &d = m_device;
  if (m_material) {
    anari::release(d, m_material);
  }
  m_material = anari::newObject<anari::Material>(d, "matte");
}

TestScene *scenePrimitives(anari::Device d)
{
  return new Primitives(d);
}
} // namespace scenes
} // namespace anari

