// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari_viewer/windows/Viewport.h"

#include "PerformanceMetrics.h"

#include <memory>

namespace anari_cat::windows {
struct PerformanceMetricsViewer : public anari_viewer::windows::Overlay
{
  PerformanceMetricsViewer(
      std::shared_ptr<anari_cat::Recorder> frameMetricsRecorder);
  ~PerformanceMetricsViewer();

  void buildUI(anari_viewer::windows::Viewport *viewport) override;

 private:
  void drawMetric(int type, const anari_cat::ScrollingBuffer &metrics);

  std::shared_ptr<anari_cat::Recorder> m_frameMetricsRecorder;
  float m_windowSizeInSeconds = 4.0f;
  float m_uptime = 0.0f;
  bool m_show{true};
  bool m_showStatistics{false};
};
} // namespace anari_cat::windows
