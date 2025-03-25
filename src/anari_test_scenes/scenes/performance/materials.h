// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"
#include "anari/anari_cpp.hpp"

namespace anari {
namespace scenes {
TestScene *sceneMaterials(anari::Device d);

struct Materials : public TestScene
{
  Materials(anari::Device d);
  ~Materials() override;

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

  bool animated() const override
  {
    return true;
  }

  void computeNextFrame() override;

 private:
  anari::World m_world{nullptr};
  anari::Surface m_surface{nullptr};
  anari::Geometry m_geometry{nullptr};

  std::vector<anari::Material> m_materials;

  std::vector<float> m_f32_array;
  std::vector<math::float2> m_f32_vec2_array;
  std::vector<math::float3> m_f32_vec3_array;
  std::vector<math::float4> m_f32_vec4_array;

  float m_timestamp = 0;
  float m_elapsed_time = 0;

  void generateGeometry();
  void updateGeometryAttributes(const std::uint32_t materialIdx);
};
} // namespace scenes
} // namespace anari