// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sidecar.h"
// nlohmann json (vendored with the glTF loader)
#include "scenes/file/nlohmann/json.hpp"
// std
#include <atomic>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

namespace anari {
namespace cts {

using nlohmann::json;

namespace {

std::filesystem::path temporarySidecarPath(
    const std::filesystem::path &path, const char *purpose)
{
  static std::atomic_uint64_t nextId{0};
  return path.parent_path()
      / ("." + path.filename().string() + "." + purpose + "."
          + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)));
}

} // namespace

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

Verdict verdictFromName(const std::string &name)
{
  if (name == "passed")
    return Verdict::Passed;
  if (name == "failed")
    return Verdict::Failed;
  return Verdict::Skipped;
}

std::string toJson(const CaseResult &result)
{
  json j;
  j["schemaVersion"] = kSidecarSchemaVersion;
  j["category"] = result.category;
  j["test"] = result.test;
  j["description"] = result.description;
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

namespace {

// A finite number, or NaN for JSON null / non-numbers. The writer emits
// non-finite scores (e.g. PSNR of identical images) as null; reading them back
// as NaN keeps them out of numeric comparisons without a separate presence
// flag.
double numberOrNan(const json &j)
{
  return j.is_number() ? j.get<double>()
                       : std::numeric_limits<double>::quiet_NaN();
}

void readMetricMap(const json &obj, std::map<std::string, double> &out)
{
  if (!obj.is_object())
    return;
  for (const auto &[name, value] : obj.items())
    out[name] = numberOrNan(value);
}

} // namespace

bool fromJson(const std::string &text, CaseResult &out, bool *schemaMismatch)
{
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (!j.is_object() || !j.contains("verdict"))
    return false; // not a sidecar

  if (schemaMismatch)
    *schemaMismatch = j.value("schemaVersion", 0) != kSidecarSchemaVersion;

  out = CaseResult{};
  out.category = j.value("category", "");
  out.test = j.value("test", "");
  out.description = j.value("description", "");
  out.caseId = j.value("case", "");
  out.groundTruthKey = j.value("groundTruthKey", "");
  if (const auto d = j.find("device"); d != j.end() && d->is_object()) {
    out.device.library = d->value("library", "");
    out.device.device = d->value("device", "default");
    out.device.renderer = d->value("renderer", "default");
  }
  out.verdict = verdictFromName(j.value("verdict", "skipped"));
  out.skipReason = j.value("skipReason", "");
  out.detail = j.value("detail", "");
  out.durationMs = j.value("durationMs", 0.0);

  if (const auto axes = j.find("axes"); axes != j.end() && axes->is_array())
    for (const auto &ax : *axes)
      out.axisValues.emplace_back(ax.value("name", ""), ax.value("value", ""));

  if (const auto chs = j.find("channels"); chs != j.end() && chs->is_array()) {
    for (const auto &c : *chs) {
      ChannelResult ch;
      ch.channel = channelFromName(c.value("channel", "color"));
      ch.passed = c.value("passed", false);
      ch.resultImage = c.value("resultImage", "");
      ch.groundTruthImage = c.value("groundTruthImage", "");
      ch.diffImage = c.value("diffImage", "");
      ch.thresholdImage = c.value("thresholdImage", "");
      readMetricMap(c.value("metrics", json::object()), ch.metrics);
      readMetricMap(c.value("thresholds", json::object()), ch.thresholds);
      out.channels.push_back(std::move(ch));
    }
  }

  return true;
}

bool readSidecar(
    const std::filesystem::path &path, CaseResult &out, bool *schemaMismatch)
{
  std::ifstream in(path);
  if (!in)
    return false;
  std::stringstream buffer;
  buffer << in.rdbuf();
  return fromJson(buffer.str(), out, schemaMismatch);
}

bool writeSidecar(const std::filesystem::path &path, const CaseResult &result)
{
  if (path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
      return false;
  }

  const auto stagedPath = temporarySidecarPath(path, "stage");
  {
    std::ofstream out(stagedPath);
    if (!out)
      return false;
    out << toJson(result) << '\n';
    out.close();
    if (!out) {
      std::error_code ec;
      std::filesystem::remove(stagedPath, ec);
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::path backupPath;
  if (std::filesystem::exists(path, ec)) {
    if (ec || !std::filesystem::is_regular_file(path, ec) || ec) {
      std::filesystem::remove(stagedPath, ec);
      return false;
    }
    backupPath = temporarySidecarPath(path, "backup");
    std::filesystem::rename(path, backupPath, ec);
    if (ec) {
      std::filesystem::remove(stagedPath, ec);
      return false;
    }
  } else if (ec) {
    std::filesystem::remove(stagedPath, ec);
    return false;
  }

  std::filesystem::rename(stagedPath, path, ec);
  if (ec) {
    std::filesystem::remove(stagedPath, ec);
    if (!backupPath.empty())
      std::filesystem::rename(backupPath, path, ec);
    return false;
  }

  if (!backupPath.empty())
    std::filesystem::remove(backupPath, ec);
  return true;
}

} // namespace cts
} // namespace anari
