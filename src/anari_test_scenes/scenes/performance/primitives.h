// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {
TestScene *scenePrimitives(anari::Device d);

struct Primitives : public TestScene
{
  Primitives(anari::Device d);
  ~Primitives() override;

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  void commit() override;

  static anari::Geometry generateGeometry(anari::Device d,
      std::uint32_t numObjects,
      const char *subtype,
      bool enableCapping);

 private:
  anari::World m_world{nullptr};
  anari::Geometry m_geometry{nullptr};
  anari::Material m_material{nullptr};

  void generateGeometry();
  void generateMaterial();
};
} // namespace scenes
} // namespace anari