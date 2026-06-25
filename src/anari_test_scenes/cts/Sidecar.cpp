// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sidecar.h"
// nlohmann json (vendored with the glTF loader)
#include "scenes/file/nlohmann/json.hpp"
// std
#include <fstream>

namespace anari {
namespace cts {

using nlohmann::json;

const char *verdictName(Verdict v)
{
  switch (v) {
  case Verdict::Passed:
    return "passed";
  case Verdict::Failed:
    return "failed";
  case Verdict::Skipped:
    return "skipped";
  }
  return "skipped";
}

std::string toJson(const CaseResult &result)
{
  json j;
  j["schemaVersion"] = kSidecarSchemaVersion;
  j["category"] = result.category;
  j["test"] = result.test;
  j["case"] = result.caseId;
  j["groundTruthKey"] = result.groundTruthKey;
  // Which device produced this result (schema v2): the diff tool labels runs
  // by this identity instead of by workdir name.
  j["device"] = {{"library", result.device.library},
      {"device", result.device.device},
      {"renderer", result.device.renderer}};
  j["verdict"] = verdictName(result.verdict);
  j["skipReason"] = result.skipReason;
  j["detail"] = result.detail;
  j["durationMs"] = result.durationMs;

  // Ordered array so axis declaration order survives the round-trip.
  j["axes"] = json::array();
  for (const auto &[name, value] : result.axisValues)
    j["axes"].push_back({{"name", name}, {"value", value}});

  j["channels"] = json::array();
  for (const auto &ch : result.channels) {
    json c;
    c["channel"] = channelName(ch.channel);
    c["passed"] = ch.passed;
    c["resultImage"] = ch.resultImage;
    c["groundTruthImage"] = ch.groundTruthImage;
    // Optional debug images; absent (empty string) when not emitted.
    c["diffImage"] = ch.diffImage;
    c["thresholdImage"] = ch.thresholdImage;
    // Non-finite scores (e.g. PSNR of identical images) serialize as null.
    c["metrics"] = json::object();
    for (const auto &[name, score] : ch.metrics)
      c["metrics"][name] = score;
    c["thresholds"] = json::object();
    for (const auto &[name, threshold] : ch.thresholds)
      c["thresholds"][name] = threshold;
    j["channels"].push_back(std::move(c));
  }

  return j.dump(2);
}

bool writeSidecar(const std::filesystem::path &path, const CaseResult &result)
{
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
      return false;
  }

  std::ofstream out(path);
  if (!out)
    return false;
  out << toJson(result) << '\n';
  return static_cast<bool>(out);
}

} // namespace cts
} // namespace anari
