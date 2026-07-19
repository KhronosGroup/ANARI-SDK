// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/HtmlReport.h"
#include "cts/Report.h"
#include "cts/Sidecar.h"
#include "cts/Workdir.h"
// std
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace anari::cts;

namespace {

CaseResult makeCase(const std::string &category,
    const std::string &test,
    const std::string &caseId,
    Verdict verdict)
{
  CaseResult r;
  r.category = category;
  r.test = test;
  r.caseId = caseId;
  r.device = {"helide", "default", "default"};
  r.verdict = verdict;
  return r;
}

ChannelResult colorChannel(double ssim, bool passed)
{
  ChannelResult ch;
  ch.channel = Channel::Color;
  ch.metrics = {{"ssim", ssim}, {"psnr", 30.0}};
  ch.thresholds = {{"ssim", 0.7}, {"psnr", 20.0}};
  ch.passed = passed;
  return ch;
}

std::map<std::string, CaseResult> keyed(std::vector<CaseResult> cases)
{
  std::map<std::string, CaseResult> out;
  for (auto &c : cases)
    out.emplace(c.category + "/" + c.test + "/" + c.caseId, std::move(c));
  return out;
}

} // namespace

// Sidecar reader /////////////////////////////////////////////////////////////

TEST_CASE("fromJson round-trips a written sidecar", "[cts][sidecar]")
{
  CaseResult r = makeCase("geometry", "sphere", "1_soup", Verdict::Passed);
  r.description = "Checks sphere geometry.";
  r.groundTruthKey = "1";
  r.axisValues = {{"primitiveCount", "1"}, {"primitiveMode", "soup"}};
  r.durationMs = 12.5;
  ChannelResult ch = colorChannel(0.95, true);
  ch.resultImage = "results/geometry/sphere/1_soup.color.png";
  ch.groundTruthImage = "ground_truth/geometry/sphere/1.color.png";
  r.channels.push_back(ch);

  CaseResult back;
  bool mismatch = true;
  REQUIRE(fromJson(toJson(r), back, &mismatch));
  CHECK_FALSE(mismatch);
  CHECK(back.category == "geometry");
  CHECK(back.test == "sphere");
  CHECK(back.caseId == "1_soup");
  CHECK(back.description == "Checks sphere geometry.");
  CHECK(back.device.library == "helide");
  CHECK(back.verdict == Verdict::Passed);
  CHECK(back.durationMs == Approx(12.5));
  REQUIRE(back.axisValues.size() == 2);
  CHECK(back.axisValues[0].first == "primitiveCount");
  REQUIRE(back.channels.size() == 1);
  CHECK(back.channels[0].channel == Channel::Color);
  CHECK(back.channels[0].metrics.at("ssim") == Approx(0.95));
  CHECK(back.channels[0].resultImage
      == "results/geometry/sphere/1_soup.color.png");
}

TEST_CASE("fromJson rejects a non-sidecar document", "[cts][sidecar]")
{
  CaseResult r;
  CHECK_FALSE(fromJson("{\"hello\": 1}", r));
  CHECK_FALSE(fromJson("not json", r));
}

TEST_CASE("fromJson reads a null metric as non-finite", "[cts][sidecar]")
{
  CaseResult r;
  REQUIRE(
      fromJson("{\"verdict\":\"passed\",\"channels\":[{\"channel\":\"color\","
               "\"metrics\":{\"psnr\":null}}]}",
          r));
  REQUIRE(r.channels.size() == 1);
  CHECK_FALSE(std::isfinite(r.channels[0].metrics.at("psnr")));
}

// Aggregation ////////////////////////////////////////////////////////////////

TEST_CASE("summarize counts overall and per category", "[cts][report]")
{
  auto results = keyed({
      makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("geometry", "cone", "b", Verdict::Failed),
      makeCase("light", "directional", "c", Verdict::Skipped),
  });

  const Summary s = summarize(results);
  CHECK(s.total == 3);
  CHECK(s.passed == 1);
  CHECK(s.failed == 1);
  CHECK(s.skipped == 1);
  CHECK(s.categories.at("geometry").passed == 1);
  CHECK(s.categories.at("geometry").failed == 1);
  CHECK(s.categories.at("light").skipped == 1);
  REQUIRE(s.failures.size() == 1);
  CHECK(s.failures[0] == "geometry/cone/b");
}

TEST_CASE(
    "runDevice returns the shared identity, none when mixed", "[cts][report]")
{
  std::ostringstream warnings;
  auto same = keyed({makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("light", "point", "b", Verdict::Passed)});
  const auto device = runDevice(same, warnings);
  REQUIRE(device.has_value());
  CHECK(device->library == "helide");
  CHECK(warnings.str().empty());

  auto mixed = same;
  CaseResult other = makeCase("volume", "field", "c", Verdict::Passed);
  other.device.library = "othurdevice";
  mixed.emplace("volume/field/c", other);
  std::ostringstream warnings2;
  CHECK_FALSE(runDevice(mixed, warnings2).has_value());
  CHECK(warnings2.str().find("mixed device") != std::string::npos);
}

// Encoding helpers ///////////////////////////////////////////////////////////

TEST_CASE("base64Encode matches RFC 4648 test vectors", "[cts][html]")
{
  CHECK(base64Encode("") == "");
  CHECK(base64Encode("f") == "Zg==");
  CHECK(base64Encode("fo") == "Zm8=");
  CHECK(base64Encode("foo") == "Zm9v");
  CHECK(base64Encode("foob") == "Zm9vYg==");
  CHECK(base64Encode("fooba") == "Zm9vYmE=");
  CHECK(base64Encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("htmlEscape neutralizes markup", "[cts][html]")
{
  CHECK(htmlEscape("<a href=\"x\">&'</a>")
      == "&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;");
}

// HTML document //////////////////////////////////////////////////////////////

TEST_CASE("generateHtml embeds summary, cases, and behavior", "[cts][html]")
{
  auto results = keyed({
      makeCase("geometry", "sphere", "a", Verdict::Passed),
      makeCase("geometry", "cone", "b", Verdict::Failed),
  });

  HtmlOptions opts; // failures-only initial view
  const std::string doc = generateHtml("/tmp/myrun", results, opts);

  CHECK(doc.find("<!DOCTYPE html>") != std::string::npos);
  CHECK(doc.find("helide (default/default)") != std::string::npos);
  CHECK(doc.find("geometry/cone/b") != std::string::npos);
  CHECK(doc.find("data-verdict=\"failed\"") != std::string::npos);
  CHECK(doc.find("data-verdict=\"passed\"") != std::string::npos); // metadata
  CHECK(doc.find("data-key=\"geometry/cone/b\"") != std::string::npos);
  CHECK(doc.find("applyFilter") != std::string::npos); // the script shipped

  // Category groups carry a deep-link anchor.
  CHECK(doc.find("id=\"cat-geometry\"") != std::string::npos);
}

TEST_CASE(
    "generateHtml references images relative to the html file", "[cts][html]")
{
  const auto root = std::filesystem::temp_directory_path() / "cts_htmlsrc_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto imgDir = root / "run" / "results" / "geometry" / "cone";
  REQUIRE(std::filesystem::create_directories(imgDir));
  std::ofstream(imgDir / "b.color.png") << "png";

  CaseResult failed = makeCase("geometry", "cone", "b", Verdict::Failed);
  ChannelResult ch = colorChannel(0.1, false);
  ch.resultImage = "results/geometry/cone/b.color.png";
  failed.channels = {ch};
  const auto results = keyed({failed});

  // A document outside the workdir reaches images through the workdir.
  HtmlOptions opts;
  opts.htmlPath = root / "reports" / "index.html";
  const std::string doc = generateHtml(root / "run", results, opts);
  CHECK(doc.find("src=\"../run/results/geometry/cone/b.color.png\"")
      != std::string::npos);

  // A document in the workdir root keeps the workdir-relative reference.
  HtmlOptions inRoot;
  inRoot.htmlPath = root / "run" / "report.html";
  const std::string rootDoc = generateHtml(root / "run", results, inRoot);
  CHECK(rootDoc.find("src=\"results/geometry/cone/b.color.png\"")
      != std::string::npos);

  // No destination: the legacy workdir-relative reference is preserved.
  const std::string bare = generateHtml(root / "run", results, HtmlOptions{});
  CHECK(bare.find("src=\"results/geometry/cone/b.color.png\"")
      != std::string::npos);

  std::filesystem::remove_all(root, ec);
}

// Results tree loading ///////////////////////////////////////////////////////

TEST_CASE("loadResults globs the results tree", "[cts][report]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_loadresults_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const Workdir wd(root);
  Case c;
  c.category = "geometry";
  c.testName = "sphere";
  c.values = {{"primitiveCount", Any(1), AxisKind::Permutation}};
  CaseResult r = makeCase("geometry", "sphere", c.id(), Verdict::Failed);
  REQUIRE(writeSidecar(wd.sidecarPath(c), r));

  std::ostringstream warnings;
  auto results = loadResults(root, warnings);
  REQUIRE(results.size() == 1);
  CHECK(results.begin()->second.verdict == Verdict::Failed);

  std::filesystem::remove_all(root, ec);
}
