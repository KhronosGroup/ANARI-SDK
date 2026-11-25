// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {

TestScene *sceneRandomCurves(anari::Device d);

struct RandomCurves : public TestScene
{
  RandomCurves(anari::Device d);
  ~RandomCurves();

  std::vector<ParameterInfo> parameters() override;

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
};

} // namespace scenes
} // namespace anari
