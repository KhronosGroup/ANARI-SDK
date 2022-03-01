// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneTexturedCubeSamplers(anari::Device d);

struct TexturedCubeSamplers : public TestScene
{
  TexturedCubeSamplers(anari::Device d);
  ~TexturedCubeSamplers();

  anari::World world() override;

  std::vector<ParameterInfo> parameters() override;

  std::vector<Camera> cameras() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari