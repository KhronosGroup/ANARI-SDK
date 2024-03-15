// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *sceneGravitySphereVolume(anari::Device d);

struct GravityVolume : public TestScene
{
  GravityVolume(anari::Device d);
  ~GravityVolume();

  std::vector<ParameterInfo> parameters() override;

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
