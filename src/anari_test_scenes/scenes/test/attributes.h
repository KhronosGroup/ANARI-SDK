// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *sceneAttributes(anari::Device d);

struct Attributes : public TestScene
{
  Attributes(anari::Device d);
  ~Attributes();

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
