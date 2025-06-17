// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "spinning_cubes.h"
#include <chrono>
#include <random>

namespace {
constexpr std::uint32_t defaultNumInstances = 512;

std::vector<anari::math::float3> vertices = {
    //
    /*0*/ {1.f, 1.f, 1.f},
    /*1*/ {1.f, 1.f, -1.f},
    /*2*/ {1.f, -1.f, 1.f},
    /*3*/ {1.f, -1.f, -1.f},
    /*4*/ {-1.f, 1.f, 1.f},
    /*5*/ {-1.f, 1.f, -1.f},
    /*6*/ {-1.f, -1.f, 1.f},
    /*7*/ {-1.f, -1.f, -1.f}
    //
};

std::vector<anari::math::uint3> indices = {
    // top
    {0, 1, 2},
    {3, 2, 1},
    // right
    {5, 1, 0},
    {0, 4, 5},
    // front
    {0, 2, 6},
    {6, 4, 0},
    // left
    {2, 3, 7},
    {7, 6, 2},
    // back
    {3, 1, 5},
    {5, 7, 3},
    // botton
    {7, 5, 4},
    {4, 6, 7},
    //
};

std::vector<anari::math::float4> colors = {
    //
    {0.f, 1.f, 0.f, 1.f},
    {0.f, 1.f, 1.f, 1.f},
    {1.f, 1.f, 0.f, 1.f},
    {1.f, 1.f, 1.f, 1.f},
    {0.f, 0.f, 0.f, 1.f},
    {0.f, 0.f, 1.f, 1.f},
    {1.f, 0.f, 0.f, 1.f},
    {1.f, 0.f, 1.f, 1.f}
    //
};
} // namespace

namespace anari {
namespace scenes {
SpinningCubes::SpinningCubes(anari::Device d)
    : TestScene(d), m_world(anari::newObject<anari::World>(m_device))
{}

SpinningCubes::~SpinningCubes()
{
  anari::release(m_device, m_group);
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> SpinningCubes::parameters()
{
  return {
      // clang-format off
      {makeParameterInfo("numInstances", "Number of instances", defaultNumInstances, 64u, 1u << 16)},
      // clang-format on
  };
}

anari::World SpinningCubes::world()
{
  return m_world;
}

void SpinningCubes::commit()
{
  auto &d = m_device;
  if (m_group) {
    anari::release(m_device, m_group);
  }

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.position",
      anari::newArray1D(d, vertices.data(), vertices.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "vertex.color",
      anari::newArray1D(d, colors.data(), colors.size()));
  anari::setAndReleaseParameter(d,
      geom,
      "primitive.index",
      anari::newArray1D(d, indices.data(), indices.size()));
  anari::commitParameters(d, geom);

  auto matte = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, matte, "color", "color");
  anari::commitParameters(d, matte);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", matte);
  anari::commitParameters(d, surface);

  m_group = anari::newObject<anari::Group>(d);
  anari::setAndReleaseParameter(
      d, m_group, "surface", anari::newArray1D(d, &surface));
  anari::release(d, surface);
  anari::commitParameters(d, m_group);

  const auto numInstances =
      getParam<std::uint32_t>("numInstances", defaultNumInstances);
  this->generateInstances(numInstances);
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
  anari::commitParameters(d, m_world);
}

void SpinningCubes::generateInstances(std::uint32_t numInstances)
{
  auto &d = m_device;
  std::vector<anari::Instance> instances;
  instances.reserve(static_cast<std::size_t>(numInstances));
  const auto now = std::chrono::duration_cast<std::chrono::duration<float>>(
      std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  std::mt19937 rng;
  std::uniform_real_distribution<float> position_dist(0.0f, 50.0f);
  for (std::uint32_t i = 0; i < numInstances; ++i) {
    const auto location = math::float3(
        position_dist(rng), position_dist(rng), position_dist(rng));
    auto translation = math::translation_matrix(4.f * location);
    const float angle_x = std::sin(location.x + now);
    const float angle_y = std::cos(location.y + now);
    const float angle_z = std::cos(location.z + now);
    auto rot_x = math::rotation_matrix(
        math::rotation_quat(math::float3(1, 0, 0), angle_x));
    auto rot_y = math::rotation_matrix(
        math::rotation_quat(math::float3(0, 1, 0), angle_y));
    auto rot_z = math::rotation_matrix(
        math::rotation_quat(math::float3(0, 0, 1), angle_z));
    math::mat4 _xfm =
        math::mul(translation, math::mul(rot_x, math::mul(rot_y, rot_z)));
    float xfm[16];
    std::memcpy(xfm, &_xfm, sizeof(_xfm));
    auto instance = anari::newObject<anari::Instance>(d, "transform");
    instances.emplace_back(instance);
    anari::setParameter(d, instance, "transform", xfm);
    anari::setParameter(d, instance, "group", m_group);
    anari::commitParameters(d, instance);
  }

  anari::setAndReleaseParameter(d,
      m_world,
      "instance",
      anari::newArray1D(d, instances.data(), instances.size()));

  for (auto &i : instances) {
    anari::release(m_device, i);
  }
}

void SpinningCubes::computeNextFrame()
{
  auto &d = m_device;
  const auto numInstances = getParam<std::uint32_t>("numInstances", defaultNumInstances);
  this->generateInstances(numInstances);
  anari::commitParameters(d, m_world);
}

TestScene *sceneSpinningCubes(anari::Device d)
{
  return new SpinningCubes(d);
}
} // namespace scenes
} // namespace anari