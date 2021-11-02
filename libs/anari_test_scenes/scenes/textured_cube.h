// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneTexturedCube(ANARIDevice d);

struct TexturedCube : public TestScene
{
  TexturedCube(ANARIDevice d);
  ~TexturedCube();

  ANARIWorld world() override;

  std::vector<Camera> cameras() override;

  void commit() override;

 private:
  ANARIWorld m_world{nullptr};
};

} // namespace scenes
} // namespace anari
