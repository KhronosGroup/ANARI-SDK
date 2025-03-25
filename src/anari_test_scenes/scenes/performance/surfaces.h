// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"
#include "anari/anari_cpp.hpp"

namespace anari {
namespace scenes {
TestScene *sceneSurfaces(anari::Device d);

struct Surfaces : public TestScene
{
  Surfaces(anari::Device d);
  ~Surfaces() override;

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

  bool animated() const override
  {
    return true;
  }

  void computeNextFrame() override;

  struct Object
  {
    math::float3 m_position;
    math::float3 m_velocity;
    math::float3 m_color;
  };

 private:
  anari::World m_world{nullptr};

  std::vector<Object> m_objects;
  float m_minDimension = -15.f;
  float m_maxDimension = 15.f;

  void generateObjects();
};
} // namespace scenes
} // namespace anari