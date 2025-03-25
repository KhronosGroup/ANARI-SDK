// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../scene.h"

namespace anari {
namespace scenes {
TestScene *sceneSpinningCubes(anari::Device d);

struct SpinningCubes : public TestScene
{
  SpinningCubes(anari::Device d);
  ~SpinningCubes() override;

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

  anari::Group m_group{nullptr};

  void generateInstances(std::uint32_t numInstances);
};
} // namespace scenes
} // namespace anari