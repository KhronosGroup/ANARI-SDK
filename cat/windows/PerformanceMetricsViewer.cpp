// Copyright 2021 Jefferson Amstutz
// SPDX-License-Identifier: Apache-2.0

#include "PerformanceMetricsViewer.h"
#include "PerformanceMetrics.h"

#include "imgui.h"
#include "implot.h"

namespace {
template <typename StyleVarT>
class ScopedImPlotStyleVar
{
 public:
  ScopedImPlotStyleVar(ImPlotStyleVar idx, StyleVarT value)
  {
    ImPlot::PushStyleVar(idx, value);
  }

  ~ScopedImPlotStyleVar()
  {
    ImPlot::PopStyleVar();
  }
};

const char *getFmtString(int metricType, bool showStatistics)
{
  switch (metricType) {
  case anari_cat::TIMESTAMP:
  case anari_cat::LATENCY_ANARI_DEVICE:
  case anari_cat::LATENCY_PRESENTATION:
  case anari_cat::LATENCY_APPLICATION:
  case anari_cat::LATENCY_UI:
  case anari_cat::TIME_SCENE_UPDATE:
  case anari_cat::TIME_SCENE_BUILD:
    return showStatistics ? "%s\n%6.2f%s\navg: %6.2f%s\n[%6.2f%s..%6.2f%s]"
                          : "%s\n%6.2f%s";
  case anari_cat::DROPPED_FRAMES:
    return showStatistics ? "%s\n%1.0f %s\navg: %1.0f %s\n[%1.0f %s..%1.0f %s]"
                          : "%s\n%1.0f %s";
  case anari_cat::COUNT:
    break;
  }
  return "";
}
} // namespace

namespace anari_cat::windows {

PerformanceMetricsViewer::PerformanceMetricsViewer(
    std::shared_ptr<anari_cat::Recorder> frameMetricsRecorder)
    : m_frameMetricsRecorder(frameMetricsRecorder)
{}

PerformanceMetricsViewer::~PerformanceMetricsViewer() = default;

void PerformanceMetricsViewer::buildUI(
    anari_viewer::windows::Viewport *viewport)
{
  m_uptime += ImGui::GetIO().DeltaTime;
  ImGui::Checkbox("perf", &m_show);
  if (!m_show) {
    return;
  }
  const auto &perfData = m_frameMetricsRecorder->GetPerformanceData();
  ImGui::Checkbox("statistics", &m_showStatistics);
  m_frameMetricsRecorder->SetComputeStatistics(m_showStatistics);

  if (!perfData.buffer.empty()) {
    for (int metricType = anari_cat::MetricType::TIMESTAMP + 1;
        metricType < anari_cat::MetricType::COUNT;
        ++metricType) {
      ImGui::PushID(metricType);
      this->drawMetric(metricType, perfData);
      ImGui::PopID();
    }
  }
}

void PerformanceMetricsViewer::drawMetric(
    const int metricType, const anari_cat::ScrollingBuffer &perfData)
{
  const float yValue = perfData.recentlyAddedElement[metricType];
  const char *units = anari_cat::GetUnits(metricType);
  ImVec4 color = {};
  if (yValue < 20) {
    // measurements less than 20ms are colored in green
    color = {0, 1, 0, 1};
  } else if (yValue < 40) {
    // measurements in the [20, 40) ms range are colored yellow.
    color = {1, 1, 0, 1};
  } else {
    // measurements in the [40, inf) ms range are colored red.
    color = {1, 0, 0, 1};
  }
  const char *label = anari_cat::GetLabel(metricType);
  // Display instantaneous, mean, min and max.
  const char *fmtString = ::getFmtString(metricType, m_showStatistics);
  if (m_showStatistics) {
    const auto &statistics = perfData.statistics[metricType];
    ImGui::TextColored(color,
        fmtString,
        label,
        yValue,
        units,
        statistics.Mean,
        units,
        statistics.Minimum,
        units,
        statistics.Maximum,
        units);
  } else {
    ImGui::TextColored(color, fmtString, label, yValue, units);
  }
  // plot time variant metrics.
  if (anari_cat::GetTimeVariance(metricType) == anari_cat::VARIANT) {
    if (m_showStatistics) {
      ImGui::SameLine(240);
    } else {
      ImGui::SameLine(100);
    }
    ScopedImPlotStyleVar plotPadding(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
    ScopedImPlotStyleVar fitPadding(
        ImPlotStyleVar_FitPadding, ImVec2(0.f, 0.2f));

    const auto plotSize = ImVec2(280, 80);
    const auto plotFlags = ImPlotFlags_CanvasOnly | ImPlotFlags_NoChild;
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4{0, 0, 0, 0});
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4{0, 0, 0, 0});
    if (ImPlot::BeginPlot("##anariCatPerf", plotSize, plotFlags)) {
      const auto xAxisFlags = ImPlotAxisFlags_NoDecorations;
      // Auto fit the limits of y-axis to the range of y-values displayed.

      const auto yAxisFlags = ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit
          | ImPlotAxisFlags_NoDecorations;
      ImPlot::SetupAxes(nullptr, nullptr, xAxisFlags, yAxisFlags);

      // Move the x-axis limits to show the last `m_windowSizeInSeconds` range
      // of values
      const float xMinimum = m_uptime - m_windowSizeInSeconds;
      const float xMaximum = m_uptime;
      const auto xLimitCondition = ImGuiCond_Always;
      ImPlot::SetupAxisLimits(ImAxis_X1, xMinimum, xMaximum, xLimitCondition);
      ImPlot::SetNextLineStyle(color);
      ImPlot::SetNextFillStyle(color, 0.5);

      const float *xValues =
          &(perfData.buffer[0][anari_cat::MetricType::TIMESTAMP]);
      const float *yValues = &(perfData.buffer[0][metricType]);
      const int count = perfData.buffer.size();
      const int offset = perfData.offset;
      const auto stride = sizeof(anari_cat::ScrollingBuffer::ElementT);

      ImPlot::PlotLine(label,
          xValues,
          yValues,
          count,
          ImPlotLineFlags_Shaded,
          offset,
          stride);

      ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor();
    ImPlot::PopStyleColor();
  }

  ImGui::Separator();
}

} // namespace anari_cat::windows
