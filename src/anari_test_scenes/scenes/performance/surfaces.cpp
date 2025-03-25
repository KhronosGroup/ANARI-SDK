// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "surfaces.h"

#include <array>
#include <chrono>
#include <random>

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"

namespace {
const char *kNumObjectsName = "numObjects";
constexpr std::uint32_t defaultNumObjects = 100;
const char *kDeltaTName = "dt";
constexpr float defaultDeltaT = 0.08f;
const std::array<anari::math::float3, 80> points = {{{-0.41714f, -0.07252f, 1.f},
    {-0.57287f, -0.07143f, 1.f},
    {0.29143f, -0.07103f, 1.f},
    {0.17305f, 0.25429f, 1.f},
    {-0.28571f, 0.25754f, 1.f},
    {0.12572f, 0.37713f, 1.f},
    {-0.24192f, 0.38f, 1.f},
    {-0.05143f, 0.90068f, 1.f},
    {0.02286f, 1.02454f, 1.f},
    {-0.13143f, 1.02644f, 1.f},
    {-0.967161f, -0.07186f, 1.f},
    {-1.69858f, -0.06878f, 1.f},
    {-1.55573f, 0.17231f, 1.f},
    {-0.972871f, 0.78092f, 1.f},
    {-0.835731f, 1.02615f, 1.f},
    {-1.69287f, 1.02723f, 1.f},
    {-2.69859f, -0.07252f, 1.f},
    {-2.85432f, -0.07143f, 1.f},
    {-1.99002f, -0.07103f, 1.f},
    {-2.1084f, 0.25429f, 1.f},
    {-2.56716f, 0.25754f, 1.f},
    {-2.15573f, 0.37713f, 1.f},
    {-2.52337f, 0.38f, 1.f},
    {-2.33288f, 0.90068f, 1.f},
    {-2.25858f, 1.02454f, 1.f},
    {-2.41288f, 1.02644f, 1.f},
    {-3.26003f, -0.07186f, 1.f},
    {-4.07791f, -0.07143f, 1.f},
    {-3.26423f, 0.40857f, 1.f},
    {-3.69658f, 0.42571f, 1.f},
    {-3.26423f, 0.55143f, 1.f},
    {-3.97073f, 0.60286f, 1.f},
    {-3.79718f, 0.60439f, 1.f},
    {-3.82575f, 0.81493f, 1.f},
    {-3.27146f, 0.90723f, 1.f},
    {-3.82575f, 0.99865f, 1.f},
    {-3.12289f, 1.02615f, 1.f},
    {-4.50559f, -0.07143f, 1.f},
    {-4.36803f, 1.02571f, 1.f},
    {-4.50824f, 1.02307f, 1.f},
    {-0.41714f, -0.07252f, 2.f},
    {-0.57287f, -0.07143f, 2.f},
    {0.29143f, -0.07103f, 2.f},
    {0.17305f, 0.25429f, 2.f},
    {-0.28571f, 0.25754f, 2.f},
    {0.12572f, 0.37713f, 2.f},
    {-0.24192f, 0.38f, 2.f},
    {-0.05143f, 0.90068f, 2.f},
    {0.02286f, 1.02454f, 2.f},
    {-0.13143f, 1.02644f, 2.f},
    {-0.967161f, -0.07186f, 2.f},
    {-1.69858f, -0.06878f, 2.f},
    {-1.55573f, 0.17231f, 2.f},
    {-0.972871f, 0.78092f, 2.f},
    {-0.835731f, 1.02615f, 2.f},
    {-1.69287f, 1.02723f, 2.f},
    {-2.69859f, -0.07252f, 2.f},
    {-2.85432f, -0.07143f, 2.f},
    {-1.99002f, -0.07103f, 2.f},
    {-2.1084f, 0.25429f, 2.f},
    {-2.56716f, 0.25754f, 2.f},
    {-2.15573f, 0.37713f, 2.f},
    {-2.52337f, 0.38f, 2.f},
    {-2.33288f, 0.90068f, 2.f},
    {-2.25858f, 1.02454f, 2.f},
    {-2.41288f, 1.02644f, 2.f},
    {-3.26003f, -0.07186f, 2.f},
    {-4.07791f, -0.07143f, 2.f},
    {-3.26423f, 0.40857f, 2.f},
    {-3.69658f, 0.42571f, 2.f},
    {-3.26423f, 0.55143f, 2.f},
    {-3.97073f, 0.60286f, 2.f},
    {-3.79718f, 0.60439f, 2.f},
    {-3.82575f, 0.81493f, 2.f},
    {-3.27146f, 0.90723f, 2.f},
    {-3.82575f, 0.99865f, 2.f},
    {-3.12289f, 1.02615f, 2.f},
    {-4.50559f, -0.07143f, 2.f},
    {-4.36803f, 1.02571f, 2.f},
    {-4.50824f, 1.02307f, 2.f}}

};

const std::array<anari::math::vec<std::uint32_t, 3>, 192> triangles = {
    {{77, 78, 79},
        {37, 38, 39},
        {72, 69, 68},
        {32, 29, 28},
        {70, 72, 68},
        {30, 32, 28},
        {76, 74, 70},
        {36, 34, 30},
        {67, 68, 69},
        {27, 28, 29},
        {64, 65, 63},
        {24, 25, 23},
        {65, 57, 62},
        {25, 17, 22},
        {76, 75, 74},
        {61, 64, 63},
        {36, 35, 34},
        {21, 24, 23},
        {74, 75, 73},
        {62, 57, 60},
        {34, 35, 33},
        {22, 17, 20},
        {61, 62, 60},
        {75, 71, 73},
        {21, 22, 20},
        {35, 31, 33},
        {64, 61, 59},
        {24, 21, 19},
        {64, 59, 58},
        {76, 70, 68},
        {24, 19, 18},
        {65, 62, 63},
        {36, 30, 28},
        {25, 22, 23},
        {76, 68, 66},
        {61, 60, 59},
        {36, 28, 26},
        {21, 20, 19},
        {72, 73, 71},
        {60, 57, 56},
        {20, 17, 16},
        {32, 33, 31},
        {71, 69, 72},
        {51, 53, 54},
        {11, 13, 14},
        {31, 29, 32},
        {54, 52, 51},
        {14, 12, 11},
        {52, 55, 51},
        {12, 15, 11},
        {50, 54, 53},
        {10, 14, 13},
        {48, 49, 47},
        {8, 9, 7},
        {49, 41, 46},
        {9, 1, 6},
        {45, 48, 47},
        {5, 8, 7},
        {46, 41, 44},
        {6, 1, 4},
        {45, 46, 44},
        {5, 6, 4},
        {48, 45, 43},
        {8, 5, 3},
        {48, 43, 42},
        {8, 3, 2},
        {49, 46, 47},
        {9, 6, 7},
        {45, 44, 43},
        {5, 4, 3},
        {44, 41, 40},
        {4, 1, 0},
        {14, 54, 10},
        {10, 54, 54},
        {10, 54, 50},
        {10, 50, 13},
        {13, 50, 50},
        {13, 50, 53},
        {9, 49, 8},
        {8, 49, 49},
        {8, 49, 48},
        {1, 41, 9},
        {9, 41, 41},
        {9, 41, 49},
        {5, 45, 7},
        {7, 45, 45},
        {7, 45, 47},
        {6, 46, 5},
        {5, 46, 46},
        {5, 46, 45},
        {2, 42, 3},
        {3, 42, 42},
        {3, 42, 43},
        {8, 48, 2},
        {2, 48, 48},
        {2, 48, 42},
        {7, 47, 6},
        {6, 47, 47},
        {6, 47, 46},
        {3, 43, 4},
        {4, 43, 43},
        {4, 43, 44},
        {0, 40, 1},
        {1, 40, 40},
        {1, 40, 41},
        {4, 44, 0},
        {0, 44, 44},
        {0, 44, 40},
        {38, 78, 37},
        {37, 78, 78},
        {37, 78, 77},
        {39, 79, 38},
        {38, 79, 79},
        {38, 79, 78},
        {37, 77, 39},
        {39, 77, 77},
        {39, 77, 79},
        {35, 75, 36},
        {36, 75, 75},
        {36, 75, 76},
        {34, 74, 33},
        {33, 74, 74},
        {33, 74, 73},
        {31, 71, 35},
        {35, 71, 71},
        {35, 71, 75},
        {26, 66, 28},
        {28, 66, 66},
        {28, 66, 68},
        {36, 76, 26},
        {26, 76, 76},
        {26, 76, 66},
        {33, 73, 32},
        {32, 73, 73},
        {32, 73, 72},
        {29, 69, 31},
        {31, 69, 69},
        {31, 69, 71},
        {32, 72, 30},
        {30, 72, 72},
        {30, 72, 70},
        {30, 70, 34},
        {34, 70, 70},
        {34, 70, 74},
        {28, 68, 27},
        {27, 68, 68},
        {27, 68, 67},
        {27, 67, 29},
        {29, 67, 67},
        {29, 67, 69},
        {25, 65, 24},
        {24, 65, 65},
        {24, 65, 64},
        {17, 57, 25},
        {25, 57, 57},
        {25, 57, 65},
        {21, 61, 23},
        {23, 61, 61},
        {23, 61, 63},
        {22, 62, 21},
        {21, 62, 62},
        {21, 62, 61},
        {18, 58, 19},
        {19, 58, 58},
        {19, 58, 59},
        {24, 64, 18},
        {18, 64, 64},
        {18, 64, 58},
        {23, 63, 22},
        {22, 63, 63},
        {22, 63, 62},
        {19, 59, 20},
        {20, 59, 59},
        {20, 59, 60},
        {16, 56, 17},
        {17, 56, 56},
        {17, 56, 57},
        {20, 60, 16},
        {16, 60, 60},
        {16, 60, 56},
        {13, 53, 11},
        {11, 53, 53},
        {11, 53, 51},
        {12, 52, 14},
        {14, 52, 52},
        {14, 52, 54},
        {15, 55, 12},
        {12, 55, 55},
        {12, 55, 52},
        {11, 51, 15},
        {15, 51, 51},
        {15, 51, 55}}};
} // namespace

namespace anari {
namespace scenes {
Surfaces::Surfaces(anari::Device d)
    : TestScene(d), m_world(anari::newObject<anari::World>(m_device))
{}

Surfaces::~Surfaces()
{
  anari::release(m_device, m_world);
}

anari::World Surfaces::world()
{
  return m_world;
}

std::vector<ParameterInfo> Surfaces::parameters()
{
  return {
      // clang-format off
      // param, description, default, value, min, max
      {makeParameterInfo(kNumObjectsName, "Number of objects", defaultNumObjects, 8u, 2000u)},
      {makeParameterInfo(kDeltaTName, "Time interval", defaultDeltaT, 0.001f, 1.f)},
      // clang-format on
  };
}

void Surfaces::commit()
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
    auto light = anari::newObject<anari::Light>(d, "directional");
    anari::setParameter(d, light, "direction", directions[i]);
    anari::setParameter(d, light, "irradiance", 1.f);
    anari::commitParameters(d, light);
    lights.emplace_back(light);
  }
  anari::setAndReleaseParameter(
      d, m_world, "light", anari::newArray1D(d, lights.data(), lights.size()));
  for (auto &l : lights) {
    anari::release(d, l);
  }
  this->generateObjects();
  anari::commitParameters(d, m_world);
}

void Surfaces::generateObjects()
{
  auto &d = m_device;
  const std::uint32_t numObjects =
      getParam<std::uint32_t>(kNumObjectsName, defaultNumObjects);
  std::vector<anari::Surface> surfaces(numObjects);
  m_objects.resize(numObjects);

  m_maxDimension = numObjects / 6.f;
  m_minDimension = -m_maxDimension;
  std::mt19937 rng;
  std::uniform_real_distribution<float> offset_dist(
      m_minDimension, m_maxDimension);
  std::normal_distribution<float> vel_dist(0.01f, 0.2f * m_maxDimension);
  std::normal_distribution<float> color_dist(0.5f, 0.5f);

  for (std::uint32_t i = 0; i < numObjects; ++i) {
    rng.seed(i);
    auto &surface = surfaces[i] = anari::newObject<anari::Surface>(d);
    auto &object = m_objects[i];

    auto geometry = anari::newObject<anari::Geometry>(d, "triangle");

    object.m_position = {offset_dist(rng), offset_dist(rng), offset_dist(rng)};
    object.m_color = {color_dist(rng), color_dist(rng), color_dist(rng)};
    object.m_velocity = {vel_dist(rng), vel_dist(rng), vel_dist(rng)};

    auto offsetedPoints = points;
    for (auto &p : offsetedPoints) {
      p += object.m_position;
    }
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, offsetedPoints.data(), offsetedPoints.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "primitive.index",
        anari::newArray1D(d, triangles.data(), triangles.size()));
    anari::commitParameters(d, geometry);
    anari::setAndReleaseParameter(d, surface, "geometry", geometry);

    auto matte = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, matte, "color", object.m_color);
    anari::commitParameters(d, matte);
    anari::setAndReleaseParameter(d, surface, "material", matte);

    anari::commitParameters(d, surface);
  }

  anari::setAndReleaseParameter(d,
      m_world,
      "surface",
      anari::newArray1D(d, surfaces.data(), surfaces.size()));
  for (auto &surface : surfaces) {
    anari::release(d, surface);
  }
}

void Surfaces::computeNextFrame()
{
  auto &d = m_device;

  const auto dt = getParam(kDeltaTName, defaultDeltaT);
  const auto numObjects = m_objects.size();

  std::uniform_real_distribution<float> offset_dist(
      m_minDimension, m_maxDimension);
  std::normal_distribution<float> vel_dist(0.01f, 0.2f * m_maxDimension);
  std::mt19937 rng;
  std::vector<anari::Surface> surfaces(numObjects);

  for (std::size_t i = 0; i < numObjects; ++i) {
    auto &surface = surfaces[i] = anari::newObject<anari::Surface>(d);
    auto &object = m_objects[i];

    auto geometry = anari::newObject<anari::Geometry>(d, "triangle");

    rng.seed(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    auto &pi = object.m_position;
    auto &vi = object.m_velocity;

    pi = pi + vi * dt;

    // Respawn at origin if logo escapes sphere.
    const auto length2 = anari::math::length2(pi);

    if (length2 > 1.5 * (m_maxDimension * m_maxDimension)) {
      pi = math::float3{0.5f * (m_maxDimension + m_minDimension)};
      vi.x = vel_dist(rng);
      vi.y = vel_dist(rng);
      vi.z = vel_dist(rng);
    }

    auto offsetedPoints = points;
    for (auto &p : offsetedPoints) {
      p += object.m_position;
    }
    anari::setAndReleaseParameter(d,
        geometry,
        "vertex.position",
        anari::newArray1D(d, offsetedPoints.data(), offsetedPoints.size()));
    anari::setAndReleaseParameter(d,
        geometry,
        "primitive.index",
        anari::newArray1D(d, triangles.data(), triangles.size()));
    anari::commitParameters(d, geometry);
    anari::setAndReleaseParameter(d, surface, "geometry", geometry);

    auto matte = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, matte, "color", object.m_color);
    anari::commitParameters(d, matte);
    anari::setAndReleaseParameter(d, surface, "material", matte);

    anari::commitParameters(d, surface);
  }
  anari::setAndReleaseParameter(d,
      m_world,
      "surface",
      anari::newArray1D(d, surfaces.data(), surfaces.size()));
  for (auto &surface : surfaces) {
    anari::release(d, surface);
  }
  anari::commitParameters(d, m_world);
}

TestScene *sceneSurfaces(anari::Device d)
{
  return new Surfaces(d);
}

} // namespace scenes
} // namespace anari