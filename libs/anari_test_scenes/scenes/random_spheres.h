// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scene.h"

namespace anari {
namespace scenes {

TestScene *sceneRandomSpheres(ANARIDevice d);

struct RandomSpheres : public TestScene
{
  RandomSpheres(ANARIDevice d);
  ~RandomSpheres();

  std::vector<ParameterInfo> parameters() override;

  ANARIWorld world() override;

  void commit() override;

 private:
  ANARIWorld m_world{nullptr};
};

} // namespace scenes
} // namespace anari
