// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari_test_scenes.h>
// match3D
#include <match3D/match3D.h>
// std
#include <functional>
#include <string>
#include <vector>

namespace windows {

using SceneSelectionCallback = std::function<void(const char *, const char *)>;

struct SceneSelector : public match3D::Window
{
  SceneSelector(const char *name = "Scene");
  ~SceneSelector();

  void buildUI() override;

  void setCallback(SceneSelectionCallback cb);

  void setScene(anari::scenes::SceneHandle scene);

 private:
  void notify();

  std::vector<std::string> m_categories;
  std::vector<std::vector<std::string>> m_scenes;

  SceneSelectionCallback m_callback;

  anari::scenes::SceneHandle m_scene{nullptr};

  int m_currentCategory{0};
  int m_currentScene{0};
};

} // namespace windows
