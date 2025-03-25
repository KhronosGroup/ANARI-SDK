// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "anari_viewer/Application.h"
#include "anari_viewer/windows/Viewport.h"

#include "PerformanceMetrics.h"

namespace anari_cat {
class Application : anari_viewer::Application
{
 public:
  explicit Application(int argc, char *argv[]);
  ~Application();

  anari_viewer::WindowArray setupWindows() override;
  void mainLoopStart() override;
  void uiFrameStart() override;
  void uiRenderStart() override;
  void uiRenderEnd() override;
  void uiFrameEnd() override;
  void mainLoopEnd() override;
  void teardown() override;

  int exec();

 private:
  bool m_animate = false;
  bool m_gui = false;
  bool m_orthographic = false;
  bool m_verbose = false;
  float m_timespan = 60.0f; // 1 minute
  std::size_t m_device_id = 0;
  std::size_t m_num_frames = 0;
  std::size_t m_capture_screenshot = 0;
  std::string m_library = "helide";
  std::string m_log_metrics = "metrics.csv";
  std::string m_renderer = "default";
  std::string m_scene = "perf:particles";
  std::vector<unsigned int> m_size = {1920, 1080};

  struct Impl;
  std::unique_ptr<Impl> m_impl;

  enum ListItemType
  {
    Devices,
    Renderers,
    Scenes
  };

  bool getANARIDevices();
  bool getANARIRenderers();
  bool getANARITestScenes();

  void onList(ListItemType type);
  void onRun();

  void logMetrics(const ScrollingBuffer::ElementT &metricData);
  void logProgress();

  static void onViewportFrameReady(const void *userPtr,
      const anari_viewer::windows::Viewport *viewport,
      const float duration);
};

} // namespace anari_cat
