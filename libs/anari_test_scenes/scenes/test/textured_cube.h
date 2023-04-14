// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *sceneTexturedCube(anari::Device d);

struct TexturedCube : public TestScene
{
  TexturedCube(anari::Device d);
  ~TexturedCube();

  anari::World world() override;

  std::vector<Camera> cameras() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
