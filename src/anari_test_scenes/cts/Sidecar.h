// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Channel.h"
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
//
// v2 adds the `device` object identifying which device produced the run, so a
// two-device diff can label runs by device rather than by workdir name.
// `description` is additive optional catalog metadata; readers treat its
// absence in older v2 workdirs as an empty description.
constexpr int kSidecarSchemaVersion = 2;

// A Case's pass/fail outcome.
enum class Verdict
{
  Passed,
  Failed,
  Skipped,
};

const char *verdictName(Verdict v);

// Which device produced a run, by the names the CLI loaded it with: the ANARI
// library, the device subtype, and the default renderer subtype. For a suite
// whose purpose is comparing devices, this is what labels a run.
struct DeviceSpec
{
  std::string library;
  std::string device{"default"};
  std::string renderer{"default"};
};

// One channel's comparison against ground truth.
struct ChannelResult
{
  Channel channel{Channel::Color};
  std::map<std::string, double> metrics; // metric name -> score
  std::map<std::string, double> thresholds; // metric name -> threshold
  bool passed{false};
  std::string resultImage; // path relative to the workdir root
  std::string groundTruthImage; // path relative to the workdir root
  // Optional per-channel debug images written when a comparison runs: the
  // absolute per-channel difference and a thresholded mask of it. Empty when
  // not emitted. Paths are relative to the workdir root, like the others.
  std::string diffImage;
  std::string thresholdImage;
};

// The full per-Case result.
struct CaseResult
{
  std::string category;
  std::string test;
  std::string description; // optional human-readable summary of the Test
  std::string caseId;
  std::string groundTruthKey;
  DeviceSpec device; // which device produced this result (schema v2)
  std::vector<std::pair<std::string, std::string>> axisValues; // name -> value
  Verdict verdict{Verdict::Skipped};
  std::string skipReason; // empty unless skipped
  std::string detail; // human-readable note (e.g. a behavioral check's outcome)
  double durationMs{0.0};
  std::vector<ChannelResult> channels;
};

// Serialize a CaseResult to its sidecar JSON text (pretty-printed).
std::string toJson(const CaseResult &result);

// Write the sidecar through a temporary sibling and atomically rename it into
// place, creating parent directories. False on failure.
bool writeSidecar(const std::filesystem::path &path, const CaseResult &result);

} // namespace cts
} // namespace anari
