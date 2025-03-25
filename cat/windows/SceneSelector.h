// Copyright 2023-2024 The Khronos Group
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
// performance
#include "PerformanceMetrics.h"

namespace anari_cat::windows {

using SceneSelectionCallback = std::function<void(const char *, const char *)>;

struct SceneSelector : public anari_viewer::windows::Window
{
  SceneSelector(const char *name = "Scene",
      int currentCategory = 0,
      int currentScene = 0,
      bool initAnimate = false);
  ~SceneSelector();

  void buildUI() override;

  void setPerformanceRecorder(std::shared_ptr<anari_cat::Recorder> recorder)
  {
    m_perfRecorder = recorder;
  }

  void setCallback(SceneSelectionCallback cb);

  void setScene(anari::scenes::SceneHandle scene);

 private:
  void notify();

  std::vector<std::string> m_categories;
  std::vector<std::vector<std::string>> m_scenes;

  SceneSelectionCallback m_callback;

  anari::scenes::SceneHandle m_scene{nullptr};
  helium::ParameterInfoList m_parameters;

  int m_currentCategory{2};
  int m_currentScene{0};

  std::shared_ptr<anari_cat::Recorder> m_perfRecorder;
  bool m_animate{false};
};

} // namespace anari_cat::windows
