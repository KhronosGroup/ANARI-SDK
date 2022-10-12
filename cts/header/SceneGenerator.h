// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scenes/scene.h"

#include <functional>

namespace cts {


class SceneGenerator : public anari::scenes::TestScene
{
 public:
  SceneGenerator(const std::string &library,
      const std::string &device,
      const std::function<void(const std::string message)> &callback);
  ~SceneGenerator();

  std::vector<anari::scenes::ParameterInfo> parameters() override;

  anari::World world() override;

  void commit() override;

  std::vector<uint8_t> renderScene(const std::string &rendererType);

 private:
  anari::Library m_library{nullptr};
  anari::Device m_device{nullptr};
  anari::World m_world{nullptr};
  std::vector<glm::vec3> generateTriangles(
      const std::string &primitiveMode, size_t primitiveCount);
};

} // namespace cts
