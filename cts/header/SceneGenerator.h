// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scenes/scene.h"

namespace cts {

anari::scenes::TestScene *sceneGenerator(anari::Device d);

struct SceneGenerator : public anari::scenes::TestScene
{
  SceneGenerator(anari::Device d);
  ~SceneGenerator();

  std::vector<anari::scenes::ParameterInfo> parameters() override;

  anari::World world() override;

  void commit() override;

 private:
  anari::World m_world{nullptr};
  std::vector<glm::vec3> generateTriangles(
      const std::string &primitiveMode, size_t primitiveCount);
};

} // namespace cts
