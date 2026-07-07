// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Report.h"
// std
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <tuple>

namespace anari {
namespace cts {

const std::vector<std::string> kReportMetrics = {"ssim", "psnr"};

namespace {

std::string caseKey(const CaseResult &r)
{
  return r.category + "/" + r.test + "/" + r.caseId;
}

// A finite score formatted like the Python reader, or "null" for non-finite.
std::string metricText(double value)
{
  if (!std::isfinite(value))
    return "null";
  std::ostringstream os;
  os << value;
  return os.str();
}

// The per-channel "metric=score" list a summary prints for one Case.
std::string channelScores(const CaseResult &r)
{
  std::ostringstream os;
  bool firstChannel = true;
  for (const auto &ch : r.channels) {
    if (!firstChannel)
      os << "; ";
    firstChannel = false;
    os << channelName(ch.channel) << "(";
    bool firstMetric = true;
    for (const auto &m : kReportMetrics) {
      auto it = ch.metrics.find(m);
      if (it == ch.metrics.end())
        continue;
      if (!firstMetric)
        os << ", ";
      firstMetric = false;
      os << m << "=" << metricText(it->second);
    }
    os << ")";
  }
  return os.str();
}

} // namespace

std::map<std::string, CaseResult> loadResults(
    const std::filesystem::path &workdir, std::ostream &warnings)
{
  std::map<std::string, CaseResult> out;
  const auto results = workdir / "results";
  std::error_code ec;
  if (!std::filesystem::is_directory(results, ec))
    return out;

  // Deterministic order so the "first read wins" duplicate rule is stable.
  std::vector<std::filesystem::path> paths;
  for (auto &entry : std::filesystem::recursive_directory_iterator(results, ec))
    if (entry.path().extension() == ".json")
      paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());

  for (const auto &path : paths) {
    CaseResult r;
    bool schemaMismatch = false;
    if (!readSidecar(path, r, &schemaMismatch))
      continue; // not a sidecar
    if (schemaMismatch)
      warnings << "warning: " << path
               << " has an unexpected schemaVersion (expected "
               << kSidecarSchemaVersion << "); reading best-effort\n";
    const auto key = caseKey(r);
    if (out.count(key)) {
      warnings << "warning: duplicate logical Case key " << key << " in "
               << path << "; keeping the first\n";
      continue;
    }
    out.emplace(key, std::move(r));
  }
  return out;
}

Summary summarize(const std::map<std::string, CaseResult> &results)
{
  Summary s;
  for (const auto &[key, r] : results) {
    ++s.total;
    auto &cat = s.categories[r.category];
    switch (r.verdict) {
    case Verdict::Passed:
      ++s.passed;
      ++cat.passed;
      break;
    case Verdict::Failed:
      ++s.failed;
      ++cat.failed;
      s.failures.push_back(key);
      break;
    case Verdict::Skipped:
      ++s.skipped;
      ++cat.skipped;
      break;
    }
  }
  return s;
}

std::optional<DeviceSpec> runDevice(
    const std::map<std::string, CaseResult> &results, std::ostream &warnings)
{
  std::map<std::tuple<std::string, std::string, std::string>, DeviceSpec>
      devices;
  for (const auto &[key, r] : results) {
    if (r.device.library.empty())
      continue;
    devices.insert(
        {{r.device.library, r.device.device, r.device.renderer}, r.device});
  }
  if (devices.size() > 1) {
    warnings << "warning: mixed device identities in one run:";
    for (const auto &[id, dev] : devices)
      warnings << " " << dev.library << " (" << dev.device << "/"
               << dev.renderer << ")";
    warnings << "\n";
    return std::nullopt;
  }
  if (devices.empty())
    return std::nullopt;
  return devices.begin()->second;
}

std::string deviceLabel(const std::map<std::string, CaseResult> &results,
    const std::string &fallback,
    std::ostream &warnings)
{
  const auto device = runDevice(results, warnings);
  if (!device)
    return fallback;
  return device->library + " (" + device->device + "/" + device->renderer + ")";
}

std::vector<std::string> reportCaseKeys(
    const std::map<std::string, CaseResult> &results, bool includeAll)
{
  if (!includeAll)
    return summarize(results).failures;
  std::vector<std::string> keys;
  keys.reserve(results.size());
  for (const auto &[key, r] : results)
    keys.push_back(key);
  return keys;
}

void writeTextSummary(std::ostream &out,
    const std::string &workdir,
    const std::map<std::string, CaseResult> &results,
    bool includeAll)
{
  const Summary s = summarize(results);
  out << "CTS report: " << workdir << "\n";
  if (const auto device = runDevice(results, out))
    out << "  device: " << device->library << " (" << device->device << "/"
        << device->renderer << ")\n";
  out << "  " << s.total << " cases: " << s.passed << " passed, " << s.failed
      << " failed, " << s.skipped << " skipped\n";
  out << "  by category:\n";
  for (const auto &[cat, c] : s.categories)
    out << "    " << std::left << std::setw(12) << cat << std::right
        << std::setw(4) << c.passed << " passed  " << std::setw(4) << c.failed
        << " failed  " << std::setw(4) << c.skipped << " skipped\n";

  const auto keys = reportCaseKeys(results, includeAll);
  if (keys.empty())
    return;
  out << "  " << (includeAll ? "all cases" : "failed cases") << ":\n";
  for (const auto &key : keys) {
    const auto &r = results.at(key);
    const std::string scores = channelScores(r);
    // Cases with no channel scores (behavioral, or a failed render) carry
    // their reason in detail or skipReason instead.
    const std::string reason = !r.detail.empty() ? r.detail : r.skipReason;
    const std::string verdict =
        includeAll ? std::string(" [") + verdictName(r.verdict) + "]" : "";
    out << "    " << key << verdict << "  "
        << (!scores.empty() ? scores : reason) << "\n";
    if (!r.description.empty())
      out << "      Description: " << r.description << "\n";
  }
}

} // namespace cts
} // namespace anari
