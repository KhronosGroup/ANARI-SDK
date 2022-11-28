// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "scenes/scene.h"

#include <functional>
#include <optional>
#include <random>
#include <unordered_map>

namespace cts {

class SceneGenerator : public anari::scenes::TestScene
{
 public:
  SceneGenerator(anari::Device device);
  ~SceneGenerator();
  std::vector<anari::scenes::ParameterInfo> parameters() override;
  void resetAllParameters();
  void resetSceneObjects();

  anari::World world() override;

  void commit() override;

  std::vector<std::vector<uint32_t>> renderScene(
      const std::string &rendererType, float renderDistance);
  std::vector<std::vector<std::vector<std::vector<float>>>> getBounds();

  float getFrameDuration() const
  {
    return frameDuration;
  }

 private:

  float frameDuration = -1.0f;

  anari::World m_world{nullptr};
  std::unordered_map<int, std::vector<anari::Object>> m_anariObjects;
};

} // namespace cts
