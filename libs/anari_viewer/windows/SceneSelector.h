// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../ui_anari.h"
// std
#include <functional>
#include <string>
#include <vector>

#include "Window.h"

namespace windows {

using SceneSelectionCallback = std::function<void(const char *, const char *)>;

struct SceneSelector : public anari_viewer::Window
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
  ui::ParameterList m_parameters;

  int m_currentCategory{0};
  int m_currentScene{0};
};

} // namespace windows
