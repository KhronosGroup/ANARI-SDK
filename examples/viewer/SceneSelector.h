// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <functional>
#include <string>
#include <vector>
// anari_test_scenes
#include "anari_test_scenes.h"
// anari_viewer
#include "anari_viewer/windows/Window.h"

namespace anari_viewer::windows {

using SceneSelectionCallback = std::function<void(const char *, const char *)>;

struct SceneSelector : public Window
{
  SceneSelector(Application *app, const char *name = "Scene");
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
  helium::ParameterInfoList m_parameters;

  int m_currentCategory{0};
  int m_currentScene{0};
};

} // namespace anari_viewer::windows
