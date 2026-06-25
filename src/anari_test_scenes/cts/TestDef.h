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

// Optional per-Case renderer builder. By default the runner uses a parameterless
// "default" renderer; a Test that exercises renderer parameters (ambient,
// background) supplies one. Returns a committed renderer the runner owns.
using RendererFn = std::function<anari::Renderer(BuildContext &)>;

// A single registered definition in the Catalog: a world-build function plus
// its axes, required features, thresholds, and bounds tolerance. A Test expands
// into one or more Cases.
struct TestDef
{
  std::string category;
  std::string name;
  BuildFn build;
  CameraFn cameraBuild;     // empty -> runner frames camera from world bounds
  RendererFn rendererBuild; // empty -> runner uses a "default" renderer
  std::vector<Axis> axes;
  std::vector<std::string> requiredFeatures;
  std::map<std::string, double> thresholds; // metric name -> threshold
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
};

} // namespace cts
} // namespace anari
