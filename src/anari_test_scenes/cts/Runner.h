// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Catalog.h"
#include "Image.h"
#include "TestDef.h"
#include "Workdir.h"
#include "anari_test_scenes_export.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <set>
#include <string>
#include <vector>

namespace anari {
namespace cts {

struct RunOptions
{
  uint32_t width{256};
  uint32_t height{256};
  double ssimThreshold{0.70}; // ctsUtility defaults; overridden per-test
  double psnrThreshold{20.0};
};

struct RunSummary
{
  int total{0};
  int passed{0}; // generated, or passed comparison
  int failed{0};
  int skipped{0};
};

// Build, render, and score Cases against ground truth, writing images and
// per-case sidecars under a Workdir (ADR-0001/0003). One Runner wraps one
// device: `generate` is driven with the reference device, `run` with a
// candidate. Cameras are framed from world bounds.
class ANARI_TEST_SCENES_INTERFACE Runner
{
 public:
  Runner(anari::Device device, Workdir workdir, RunOptions options = {});

  // Render every selected Case and save its channels under ground_truth/.
  // Does not score or write sidecars (ADR-0005).
  RunSummary generate(const Catalog &catalog,
      const Filter &filter,
      const std::set<std::string> &referenceFeatures);

  // Render every selected Case, compare each channel against ground truth, and
  // write result images plus a sidecar. Cases whose required features are
  // absent, or that have no ground truth yet, are recorded as skipped.
  RunSummary run(const Catalog &catalog,
      const Filter &filter,
      const std::set<std::string> &candidateFeatures);

  // Render a single Case's channels (in TestDef::channels order). Empty if the
  // world could not be built. Public for testing and reuse.
  std::vector<Image> renderCase(const TestDef &test, const Case &c);

 private:
  // The committed render objects for one Case: the world plus the camera and
  // renderer (either the Test's build hooks or the runner's defaults). `world`
  // is null when the Test has no build function or build() returned null.
  struct SceneObjects
  {
    anari::World world{nullptr};
    anari::Camera camera{nullptr};
    anari::Renderer renderer{nullptr};
    anari::scenes::Bounds bounds{};

    // A renderable scene needs all three objects; a build hook (or default
    // camera/renderer) returning null leaves it invalid.
    bool valid() const
    {
      return world && camera && renderer;
    }
  };

  // Build a Case's world, camera, and renderer. The caller owns the result and
  // must releaseScene() it (even when !valid(), to release a partial build).
  SceneObjects buildScene(const TestDef &test, const Case &c);
  void releaseScene(SceneObjects &scene);

  // Write a "missing required feature" skip sidecar for a Case and tally it.
  void writeFeatureSkip(const Case &c, RunSummary &summary);

  // Write a failed sidecar for a Case whose build/render threw, recording the
  // reason in `detail`, and tally it. Per-case crash isolation (ADR-0003): a
  // throwing case is contained so the run continues.
  void writeCaseFailure(
      const Case &c, const std::string &detail, RunSummary &summary);

  // Run a behavioral Test (TestDef::behaviorCheck set): one Case at a time,
  // feature-gating then invoking the check and writing its verdict + detail.
  void runBehaviorTest(const TestDef &test,
      const std::set<std::string> &candidateFeatures,
      RunSummary &summary);

  anari::Device m_device{nullptr};
  Workdir m_workdir;
  RunOptions m_options;
};

} // namespace cts
} // namespace anari
