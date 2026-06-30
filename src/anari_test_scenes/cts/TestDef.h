// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Axis.h"
#include "BuildContext.h"
#include "Channel.h"
#include "anari_test_scenes.h" // anari::scenes::Bounds / Camera
// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// A test's world-build function: reads the current Case's axis values from the
// context, authors an ANARI world with normal ANARI C++ calls plus the shared
// world-build helpers, and returns the World. The harness owns the returned
// World and releases it.
using BuildFn = std::function<anari::World(BuildContext &)>;

// Optional per-Case camera builder. By default the runner frames a perspective
// camera from the world's bounds; a Test that exercises camera parameters
// supplies one of these instead, reading axis values from the context and the
// world bounds the runner computed. Returns a committed camera the runner owns.
using CameraFn =
    std::function<anari::Camera(BuildContext &, const anari::scenes::Bounds &)>;

// Optional per-Case renderer configuration. The runner creates the configured
// renderer subtype and applies its baseline parameters first; a Test that
// exercises renderer parameters (ambient, background) can then override them.
// The runner commits and owns the renderer.
using RendererFn = std::function<void(BuildContext &, anari::Renderer)>;

// The outcome of a behavioral check: a self-contained pass/fail with a
// human-readable detail. Distinct from render-and-compare scoring.
struct BehaviorResult
{
  bool passed{false};
  std::string detail;
};

// Optional per-Case behavioral check, for conformance guarantees the
// render-and-compare path can't express (e.g. a frame-completion callback
// firing, progressive accumulation converging). When a Test sets one, the
// runner builds the world/camera/renderer, calls this instead of rendering and
// scoring against ground truth, and records the returned verdict + detail in
// the sidecar. No ground truth is generated or needed for such a Test.
using BehaviorFn = std::function<BehaviorResult(anari::Device,
    anari::World,
    anari::Camera,
    anari::Renderer,
    uint32_t width,
    uint32_t height)>;

// A single registered definition in the Catalog: a human-readable description,
// world-build function, axes, required features, thresholds, and bounds
// tolerance. A Test expands into one or more Cases.
struct TestDef
{
  std::string category;
  std::string name;
  std::string description;
  BuildFn build;
  CameraFn cameraBuild; // empty -> runner frames camera from world bounds
  RendererFn rendererConfig; // empty -> runner keeps its renderer baseline
  BehaviorFn behaviorCheck; // set -> verify behavior instead of render+compare
  std::vector<Axis> axes;
  std::vector<std::string> requiredFeatures;
  std::map<std::string, double>
      thresholds; // metric name -> threshold (all channels)
  // Per-channel metric thresholds, overriding `thresholds` for that channel
  // only. Most tests compare every channel against one threshold; a test that
  // needs a stricter (or looser) bar on a specific channel sets it here.
  std::map<Channel, std::map<std::string, double>> channelThresholds;
  double boundsTolerance{0.0};
  std::vector<Channel> channels{Channel::Color};
  // When set, expand one-factor-at-a-time instead of the full cartesian
  // product: a baseline of every axis's first value, plus one Case per
  // additional value of each axis.
  bool simplified{false};

  std::string id() const
  {
    return category + "/" + name;
  }

  double thresholdOr(const std::string &metric, double valIfNotFound) const
  {
    auto it = thresholds.find(metric);
    return it != thresholds.end() ? it->second : valIfNotFound;
  }

  // The threshold to apply for a metric on a specific channel: a per-channel
  // override if one is set, else the test-wide metric threshold, else the
  // supplied default (the runner's RunOptions default).
  double thresholdFor(
      Channel channel, const std::string &metric, double valIfNotFound) const
  {
    auto cit = channelThresholds.find(channel);
    if (cit != channelThresholds.end()) {
      auto mit = cit->second.find(metric);
      if (mit != cit->second.end())
        return mit->second;
    }
    return thresholdOr(metric, valIfNotFound);
  }
};

} // namespace cts
} // namespace anari
