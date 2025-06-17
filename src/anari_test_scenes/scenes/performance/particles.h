// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {
TestScene *sceneParticles(anari::Device d);

struct Particles : public TestScene
{
  Particles(anari::Device d);
  ~Particles() override;

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

  bool animated() const override
  {
    return true;
  }

  void computeNextFrame() override;

  struct Particle
  {
    anari::math::float3 m_position;
    anari::math::float4 m_color;
    anari::math::float3 m_velocity;
    float m_radius;
  };

 private:
  anari::World m_world{nullptr};

  anari::Group m_group{nullptr};

  anari::Geometry m_geometry{nullptr};

  anari::Surface generateParticles();
  std::vector<Particle> m_particles;
};
} // namespace scenes
} // namespace anari