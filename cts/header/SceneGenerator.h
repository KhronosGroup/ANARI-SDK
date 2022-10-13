// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scenes/scene.h"

#include <functional>
#include <random>

namespace cts {

class SceneGenerator : public anari::scenes::TestScene
{
 public:
  static SceneGenerator *createSceneGenerator(const std::string &library,
      const std::string &device,
      const std::function<void(const std::string message)> &callback);

  SceneGenerator(anari::Device device);
  ~SceneGenerator();

  std::vector<anari::scenes::ParameterInfo> parameters() override;
  void resetAllParameters();

  anari::World world() override;

  void commit() override;

  std::vector<std::vector<uint32_t>> renderScene(
      const std::string &rendererType);

  float getRandom(float min, float max);

 private:
  static anari::Library m_library;

  anari::World m_world{nullptr};
  std::mt19937 m_rng;

  std::vector<glm::vec3> generateTriangles(size_t primitiveCount);
  std::vector<glm::vec3> generateTriangulatedQuads(size_t primitiveCount);
  std::vector<glm::vec3> generateTriangulatedCubes(size_t primitiveCount);
};

} // namespace cts
