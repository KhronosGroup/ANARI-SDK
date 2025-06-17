// Copyright 2021-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Timer.h"

#include <imgui.h> // for ImVector

#include <array>
#include <limits>

#pragma once

namespace anari_cat {

enum MetricType : int
{
  // The timestamp (in seconds) of collected data.
  TIMESTAMP,
  // This corresponds to the duration property on ANARIFrame
  LATENCY_ANARI_DEVICE,
  // Counts how many times the event loop requested anari for a frame but the
  // frame was not yet ready.
  DROPPED_FRAMES,
  // Total time it takes from the moment ANARIFrame is ready to presentation.
  LATENCY_PRESENTATION,
  // This corresponds to the perceived latency in an interactive application.
  // Includes all internal computations, scene updates, rendering tasks and OS
  // event polling.
  LATENCY_APPLICATION,
  // Complete user interface rendering.
  LATENCY_UI,
  // Time taken to update a parameter, and commit the scene.
  TIME_SCENE_UPDATE,
  // Time taken to construct a scene.
  TIME_SCENE_BUILD,
  COUNT
};

// Defines the variation of a MetricType over time.
enum TimeVarianceType : int
{
  INVARIANT,
  VARIANT
};

inline TimeVarianceType GetTimeVariance(int metricType)
{
  switch (metricType) {
  case MetricType::TIMESTAMP:
  case MetricType::LATENCY_ANARI_DEVICE:
  case MetricType::DROPPED_FRAMES:
  case MetricType::LATENCY_PRESENTATION:
  case MetricType::LATENCY_APPLICATION:
  case MetricType::LATENCY_UI:
  case MetricType::TIME_SCENE_UPDATE:
    return VARIANT;
  case MetricType::TIME_SCENE_BUILD:
  default:
    return INVARIANT;
  }
}

inline const char *GetLabel(int metricType)
{
  switch (metricType) {
  case MetricType::TIMESTAMP:
    return "Timestamp";
  case MetricType::LATENCY_ANARI_DEVICE:
    return "Device";
  case MetricType::DROPPED_FRAMES:
    return "Dropped";
  case MetricType::LATENCY_PRESENTATION:
    return "Present";
  case MetricType::LATENCY_APPLICATION:
    return "App";
  case MetricType::LATENCY_UI:
    return "UI";
  case MetricType::TIME_SCENE_UPDATE:
    return "Scene update";
  case MetricType::TIME_SCENE_BUILD:
    return "Scene build";
  default:
    return nullptr;
  }
}

inline const char *GetUnits(int metricType)
{
  switch (metricType) {
  case MetricType::TIMESTAMP:
    return "s";
  case MetricType::LATENCY_ANARI_DEVICE:
    return "ms";
  case MetricType::DROPPED_FRAMES:
    return "frames";
  case MetricType::LATENCY_PRESENTATION:
    return "ms";
  case MetricType::LATENCY_APPLICATION:
    return "ms";
  case MetricType::LATENCY_UI:
    return "ms";
  case MetricType::TIME_SCENE_UPDATE:
    return "ms";
  case MetricType::TIME_SCENE_BUILD:
    return "ms";
  default:
    return nullptr;
  }
}

struct MetricStatistics
{
  float Mean = 0;
  float Maximum = std::numeric_limits<float>::min();
  float Minimum = std::numeric_limits<float>::max();
};

// utility structure for scrolling buffer plot
class ScrollingBuffer
{
  int m_maxSize;

 public:
  using ElementT = std::array<float, MetricType::COUNT>;

  ElementT recentlyAddedElement;
  ImVector<ElementT> buffer;
  int offset = 0;
  std::array<MetricStatistics, MetricType::COUNT> statistics = {};

  ScrollingBuffer(int maxSize = 500)
  {
    m_maxSize = maxSize;
    offset = 0;
    buffer.reserve(m_maxSize);
  }

  void AddMetrics(const ElementT &metrics)
  {
    recentlyAddedElement = metrics;
    if (buffer.size() < m_maxSize) {
      buffer.push_back(metrics);
    } else {
      buffer[offset] = metrics;
      offset = (offset + 1) % m_maxSize;
    }
  }

  void Erase()
  {
    if (buffer.size() > 0) {
      buffer.shrink(0);
      offset = 0;
    }
  }

  void ComputeStatistics()
  {
    statistics = {};
    // for each type of metric
    for (int type = 0; type < MetricType::COUNT; ++type) {
      int j = offset;
      // for each frame
      for (int i = 0; i < buffer.size(); ++i) {
        const auto value = buffer[j][type];
        statistics[type].Maximum =
            value > statistics[type].Maximum ? value : statistics[type].Maximum;
        statistics[type].Minimum =
            value < statistics[type].Minimum ? value : statistics[type].Minimum;
        statistics[type].Mean += value;
        // wrap around
        j = (j + 1) % buffer.size();
      }
      statistics[type].Mean /= buffer.size();
    }
  }
};

class Recorder
{
  ScrollingBuffer m_metricsBuffer;

  ScrollingBuffer::ElementT m_currentFrameMetrics = {};

  std::array<Timer, MetricType::COUNT> m_timers = {};

  bool m_computeStatistics = false;

 public:
  void SetComputeStatistics(bool value)
  {
    m_computeStatistics = value;
  }

  void SetMetricsValue(MetricType type, float value)
  {
    m_currentFrameMetrics[type] = value;
  }

  void StartTimer(MetricType type)
  {
    m_timers[type].start();
  }

  void StopTimer(MetricType type)
  {
    m_currentFrameMetrics[type] = m_timers[type].millisecondsElapsed();
  }

  ScrollingBuffer::ElementT Collect(float timeStamp)
  {
    // record timestamp
    m_currentFrameMetrics[MetricType::TIMESTAMP] = timeStamp;

    // ensure start/stop were called in correct order.
    assert(m_currentFrameMetrics[MetricType::LATENCY_APPLICATION]
        >= m_currentFrameMetrics[MetricType::LATENCY_UI]);

    // Update metrics buffer
    m_metricsBuffer.AddMetrics(m_currentFrameMetrics);

    // Update mean, min, max when statistics are enabled.
    if (m_computeStatistics) {
      m_metricsBuffer.ComputeStatistics();
    }

    // reset VARIANT metrics for next collection.
    auto result = m_currentFrameMetrics;
    for (int type = 0; type < MetricType::COUNT; ++type) {
      if (GetTimeVariance(type) == VARIANT) {
        m_currentFrameMetrics[type] = 0;
      }
    }
    return result;
  }

  ScrollingBuffer &GetPerformanceData()
  {
    return m_metricsBuffer;
  }
};

} // namespace anari_cat
