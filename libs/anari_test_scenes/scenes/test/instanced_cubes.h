// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *sceneInstancedCubes(anari::Device d);

struct InstancedCubes : public TestScene
{
  InstancedCubes(anari::Device d);
  ~InstancedCubes();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
