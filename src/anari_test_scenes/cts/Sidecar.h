// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Channel.h"
#include "anari_test_scenes_export.h"
// std
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

// The per-Case result the runner writes next to the rendered images. The
// results tree of these sidecars is the cross-language contract (ADR-0003);
// the Python layer consumes it by globbing and never renders. The schema is
// versioned so the reader can stay in step. New *optional* fields (read with a
// default by the Python layer) are additive and do not bump this version; a
// breaking change to an existing field's shape would.
constexpr int kSidecarSchemaVersion = 1;

// A Case's pass/fail outcome.
enum class Verdict
{
  Passed,
  Failed,
  Skipped,
};

ANARI_TEST_SCENES_INTERFACE const char *verdictName(Verdict v);

// One channel's comparison against ground truth.
struct ChannelResult
{
  Channel channel{Channel::Color};
  std::map<std::string, double> metrics; // metric name -> score
  std::map<std::string, double> thresholds; // metric name -> threshold
  bool passed{false};
  std::string resultImage; // path relative to the workdir root
  std::string groundTruthImage; // path relative to the workdir root
};

// The full per-Case result.
struct CaseResult
{
  std::string category;
  std::string test;
  std::string caseId;
  std::string groundTruthKey;
  std::vector<std::pair<std::string, std::string>> axisValues; // name -> value
  Verdict verdict{Verdict::Skipped};
  std::string skipReason; // empty unless skipped
  std::string detail; // human-readable note (e.g. a behavioral check's outcome)
  double durationMs{0.0};
  std::vector<ChannelResult> channels;
};

// Serialize a CaseResult to its sidecar JSON text (pretty-printed).
ANARI_TEST_SCENES_INTERFACE std::string toJson(const CaseResult &result);

// Write the sidecar to disk, creating parent directories. False on failure.
ANARI_TEST_SCENES_INTERFACE bool writeSidecar(
    const std::filesystem::path &path, const CaseResult &result);

} // namespace cts
} // namespace anari
