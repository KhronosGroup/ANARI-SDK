// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "particles.h"

#include <cmath>
#include <random>

#if defined(USE_KOKKOS)
#include <Kokkos_Core.hpp>
#endif

#include "anari/anari_cpp.hpp"
#include "anari/anari_cpp/ext/linalg.h"

namespace {
const char *kNumParticlesName = "numParticles";
constexpr std::uint32_t defaultNumParticles = 65536;
const char *kDeltaTName = "dt";
constexpr float defaultDeltaT = 0.08f;
const anari::math::float3 positionMin = {-5.04979f, -0.0760648f, 0.0f};
const anari::math::float3 positionMax = {-0.226393f, 1.02342f, 10.0f};
const float piTwice = 6.283185f;
const std::vector<anari::math::float3> skeletonPoints = {
    {-0.226393f, -0.0702599f, 0.f},
    {-0.640616f, 1.01592f, 0.f},
    {-0.909626f, 0.342247f, 0.f},
    {-0.428359f, 0.335865f, 0.f},
    {-0.410397f, 0.29145f, 0.f},
    {-0.921091f, 0.299406f, 0.f},
    {-1.07288f, -0.0601484f, 0.f},
    {-2.51179f, -0.0668024f, 0.f},
    {-2.92602f, 1.01938f, 0.f},
    {-3.19503f, 0.345704f, 0.f},
    {-2.71376f, 0.339322f, 0.f},
    {-2.6958f, 0.294908f, 0.f},
    {-3.20649f, 0.302864f, 0.f},
    {-3.35829f, -0.0566909f, 0.f},
    {-1.48672f, -0.0656923f, 0.f},
    {-1.48059f, 1.02342f, 0.f},
    {-2.21091f, -0.0760648f, 0.f},
    {-2.22206f, 1.01996f, 0.f},
    {-3.78941f, -0.0691498f, 0.f},
    {-3.77079f, 0.985385f, 0.f},
    {-4.3043f, 0.985385f, 0.f},
    {-4.45696f, 0.866101f, 0.f},
    {-4.49353f, 0.714836f, 0.f},
    {-4.45476f, 0.609815f, 0.f},
    {-4.3524f, 0.496798f, 0.f},
    {-3.87768f, 0.504253f, 0.f},
    {-3.87543f, 0.459576f, 0.f},
    {-4.21313f, 0.46144f, 0.f},
    {-4.43212f, 0.228991f, 0.f},
    {-4.56813f, -0.0691498f, 0.f},
    {-4.98279f, 1.006f, 0.f},
    {-4.98623f, -0.0566763f, 0.f},
    {-5.04871f, -0.0566763f, 0.f},
    {-5.04979f, 1.01073f, 0.f}};
const std::vector<anari::math::int2> skeletonLines = {
    {0, 1},
    {1, 2},
    {2, 3},
    {3, 4},
    {4, 5},
    {5, 6},
    {7, 8},
    {8, 9},
    {9, 10},
    {10, 11},
    {11, 12},
    {12, 13},
    {14, 15},
    {15, 16},
    {16, 17},
    {18, 19},
    {19, 20},
    {20, 21},
    {21, 22},
    {22, 23},
    {23, 24},
    {24, 25},
    {25, 26},
    {26, 27},
    {27, 28},
    {28, 29},
    {30, 31},
    {31, 32},
    {32, 33},
};
} // namespace

namespace anari {
namespace scenes {

Particles::Particles(anari::Device d)
    : TestScene(d), m_world(anari::newObject<anari::World>(m_device))
{}

Particles::~Particles()
{
  if (m_geometry) {
    anari::release(m_device, m_geometry);
  }
  anari::release(m_device, m_world);
}

std::vector<ParameterInfo> Particles::parameters()
{
  return {
      // clang-format off
    // param, description, default, value, min, max
    {makeParameterInfo(kNumParticlesName, "Number of particles", defaultNumParticles, 8u, 1u << 20)},
    {makeParameterInfo(kDeltaTName, "Time interval", defaultDeltaT, 0.001f, 1.f)},
      // clang-format on
  };
}

anari::World Particles::world()
{
  return m_world;
}

void Particles::commit()
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

  auto surface = this->generateParticles();
  anari::setAndReleaseParameter(
      d, m_world, "surface", anari::newArray1D(d, &surface));
  anari::release(d, surface);
  anari::commitParameters(d, m_world);
}

anari::Surface Particles::generateParticles()
{
  auto &d = m_device;

  // destroy previous geometry
  if (m_geometry) {
    anari::release(d, m_geometry);
  }

  m_geometry = anari::newObject<anari::Geometry>(d, "sphere");
  auto matte = anari::newObject<anari::Material>(d, "matte");

  std::mt19937 rng;
  std::uniform_real_distribution<float> z_dist(positionMin.z, positionMax.z);
  std::normal_distribution<float> r_dist(0.5f, 0.4f);
  std::uniform_real_distribution<float> t_dist(0.0f, 1.f);
  std::uniform_real_distribution<float> theta_dist(0.0f, piTwice);
  std::uniform_real_distribution<float> color_dist(0.0f, 1.0f);
  std::normal_distribution<float> radius_dist(
      (positionMax.x - positionMin.x) * 1e-3f,
      (positionMax.x - positionMin.x) * 2.0e-4f);
  std::uniform_real_distribution<float> line_dist(
      0, static_cast<float>(skeletonLines.size()));

  const auto numParticles = getParam(kNumParticlesName, defaultNumParticles);
  m_particles.clear();
  m_particles.reserve(numParticles);

  for (std::uint32_t i = 0; i < numParticles; ++i) {
    const std::size_t lineId =
        static_cast<std::size_t>(std::floor(line_dist(rng)));
    const auto &line = skeletonLines[lineId];
    const auto &p1 = skeletonPoints[line.x];
    const auto &p2 = skeletonPoints[line.y];
    const auto r = r_dist(rng);
    const auto t = t_dist(rng);
    const auto theta = theta_dist(rng);
    math::float3 position = p1 + t * (p2 - p1);
    position.x += r * std::cos(theta) * 0.0824f;
    position.y += r * std::sin(theta) * 0.0824f;
    position.z = z_dist(rng);
    // clang-format off
    m_particles.emplace_back(
      Particle
      {
        /*m_position=*/
        position,
        /*.m_color=*/
        {
          color_dist(rng),
          color_dist(rng),
          color_dist(rng),
          1.0f
        },
        /*.m_velocity=*/
        {
          0.0f,
          0.0f,
          0.0f
        },
        /*m_radius=*/
          radius_dist(rng),
    });
    // clang-format on
  }
  const auto *particlesData = reinterpret_cast<uint8_t *>(m_particles.data());
  anari::setParameterArray1DStrided(d,
      m_geometry,
      "vertex.position",
      ANARI_FLOAT32_VEC3,
      particlesData,
      numParticles,
      sizeof(Particle));
  anari::setParameterArray1DStrided(d,
      m_geometry,
      "vertex.color",
      ANARI_FLOAT32_VEC4,
      particlesData + offsetof(Particle, m_color),
      numParticles,
      sizeof(Particle));
  anari::setParameterArray1DStrided(d,
      m_geometry,
      "vertex.radius",
      ANARI_FLOAT32,
      particlesData + offsetof(Particle, m_radius),
      m_particles.size(),
      sizeof(Particle));
  anari::setParameter(d, matte, "color", "color");

  anari::commitParameters(d, m_geometry);
  anari::commitParameters(d, matte);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", m_geometry);
  anari::setAndReleaseParameter(d, surface, "material", matte);
  anari::commitParameters(d, surface);
  return surface;
}

namespace {
void PushParticle(Particles::Particle &particle, float dt)
{
  auto &pi = particle.m_position;
  auto &vi = particle.m_velocity;

  pi = pi + vi * dt;
  vi.z += 9.8f * dt;

  // Periodic boundaries
  if (pi.z < positionMin.z) {
    pi.z = positionMax.z;
    vi.z = 0;
  }

  if (pi.z > positionMax.z) {
    pi.z = positionMin.z;
    vi.z = 0;
  }
}
} // namespace

#if defined(USE_KOKKOS)
using view_type = Kokkos::View<Particles::Particle *>;

struct ParticlePusher
{
  view_type m_particles;
  float m_dt;
  std::size_t m_particleCount;

  ParticlePusher(view_type particles, float dt)
      : m_particles(particles), m_dt(dt), m_particleCount(particles.size())
  {}

  // The parallel_for loop will iterate
  // over the View's first dimension N.
  KOKKOS_INLINE_FUNCTION
  void operator()(const std::size_t i) const
  {
    auto &particle = m_particles(i);
    PushParticle(particle, m_dt);
  }
};
#endif

void Particles::computeNextFrame()
{
  auto &d = m_device;

  const auto dt = getParam(kDeltaTName, defaultDeltaT);
#if defined(USE_KOKKOS)
  view_type particles_view("Particles", m_particles.size());
  particles_view.assign_data(m_particles.data());

  ParticlePusher particle_pusher(particles_view, dt);
  Kokkos::parallel_for(m_particles.size(), particle_pusher);
  Kokkos::fence();
#else
  for (auto &particle : m_particles) {
    PushParticle(particle, dt);
  }
#endif
  anari::setParameterArray1DStrided(d,
      m_geometry,
      "vertex.position",
      ANARI_FLOAT32_VEC3,
      m_particles.data(),
      m_particles.size(),
      sizeof(Particle));
  anari::commitParameters(d, m_geometry);
}
TestScene *sceneParticles(anari::Device d)
{
  return new Particles(d);
}

} // namespace scenes
} // namespace anari
