// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/ArtifactPublication.h"
#include "cts/Case.h"
#include "cts/Metrics.h"
#include "cts/Sidecar.h"
#include "cts/Workdir.h"
// std
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

using namespace anari::cts;

namespace {

Case sphereCase()
{
  Case c;
  c.category = "geometry";
  c.testName = "sphere";
  c.values = {
      {"primitiveCount", Any(1), AxisKind::Permutation},
      {"primitiveMode", Any("soup"), AxisKind::Variant},
  };
  return c;
}

Image solidImage(uint8_t red, uint8_t green, uint8_t blue)
{
  Image image;
  image.width = 1;
  image.height = 1;
  image.rgba = {red, green, blue, 255};
  return image;
}

} // namespace

// Workdir paths //////////////////////////////////////////////////////////////

TEST_CASE(
    "Workdir mirrors the catalog hierarchy under each subdir", "[cts][workdir]")
{
  Workdir wd("/tmp/run");
  auto c = sphereCase();

  CHECK(wd.resultsDir() == std::filesystem::path("/tmp/run/results"));
  CHECK(wd.groundTruthDir() == std::filesystem::path("/tmp/run/ground_truth"));
  CHECK(wd.assetsDir() == std::filesystem::path("/tmp/run/assets"));

  // Result files are keyed by the unique Case id.
  CHECK(wd.sidecarPath(c)
      == std::filesystem::path("/tmp/run/results/geometry/sphere/1_soup.json"));
  CHECK(wd.resultImagePath(c, Channel::Color)
      == std::filesystem::path(
          "/tmp/run/results/geometry/sphere/1_soup.color.png"));
  CHECK(wd.resultImagePath(c, Channel::Depth)
      == std::filesystem::path(
          "/tmp/run/results/geometry/sphere/1_soup.depth.png"));

  // Debug images sit beside the result image, keyed by the same Case id.
  CHECK(wd.diffImagePath(c, Channel::Color)
      == std::filesystem::path(
          "/tmp/run/results/geometry/sphere/1_soup.color.diff.png"));
  CHECK(wd.thresholdImagePath(c, Channel::Color)
      == std::filesystem::path(
          "/tmp/run/results/geometry/sphere/1_soup.color.threshold.png"));

  // Ground truth is keyed by the shared ground-truth key (variants collapse).
  CHECK(wd.groundTruthImagePath(c, Channel::Color)
      == std::filesystem::path(
          "/tmp/run/ground_truth/geometry/sphere/1.color.png"));
}

TEST_CASE("Workdir relativeToRoot strips the root prefix", "[cts][workdir]")
{
  Workdir wd("/tmp/run");
  CHECK(wd.relativeToRoot(wd.resultImagePath(sphereCase(), Channel::Color))
      == "results/geometry/sphere/1_soup.color.png");
}

TEST_CASE(
    "Workdir writes a .gitignore that ignores everything", "[cts][workdir]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_gitignore_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Workdir wd(root);
  REQUIRE(wd.writeGitignore()); // ADR-0005: generated workdir is gitignored
  const auto ignore = root / ".gitignore";
  REQUIRE(std::filesystem::exists(ignore));

  std::ifstream in(ignore);
  std::stringstream buffer;
  buffer << in.rdbuf();
  CHECK(buffer.str().find("*") != std::string::npos);

  std::filesystem::remove_all(root, ec);
}

// Verdict ////////////////////////////////////////////////////////////////////

TEST_CASE(
    "metricPassed is a strict higher-is-better threshold", "[cts][verdict]")
{
  CHECK(metricPassed(0.8, 0.7));
  CHECK_FALSE(metricPassed(0.7, 0.7)); // strict
  CHECK_FALSE(metricPassed(0.6, 0.7));
  CHECK(
      metricPassed(std::numeric_limits<double>::infinity(), 20.0)); // identical
  CHECK_FALSE(metricPassed(std::numeric_limits<double>::quiet_NaN(), 20.0));
}

// Sidecar JSON ///////////////////////////////////////////////////////////////

TEST_CASE("sidecar JSON carries the contract fields", "[cts][sidecar]")
{
  CaseResult r;
  r.category = "geometry";
  r.test = "sphere";
  r.caseId = "1_soup";
  r.groundTruthKey = "1";
  r.device = {"helide", "default", "default"};
  r.axisValues = {{"primitiveCount", "1"}, {"primitiveMode", "soup"}};
  r.verdict = Verdict::Passed;
  r.durationMs = 12.5;

  ChannelResult ch;
  ch.channel = Channel::Color;
  ch.metrics = {{"ssim", 0.95}, {"psnr", 31.0}};
  ch.thresholds = {{"ssim", 0.7}, {"psnr", 20.0}};
  ch.passed = true;
  ch.resultImage = "results/geometry/sphere/1_soup.color.png";
  ch.groundTruthImage = "ground_truth/geometry/sphere/1.color.png";
  ch.diffImage = "results/geometry/sphere/1_soup.color.diff.png";
  ch.thresholdImage = "results/geometry/sphere/1_soup.color.threshold.png";
  r.channels.push_back(ch);

  const std::string text = toJson(r);

  // Spot-check the contract surface without depending on key ordering.
  CHECK(text.find("\"schemaVersion\": 2") != std::string::npos);
  CHECK(text.find("\"verdict\": \"passed\"") != std::string::npos);
  CHECK(text.find("\"groundTruthKey\": \"1\"") != std::string::npos);
  CHECK(text.find("\"primitiveMode\"") != std::string::npos);
  CHECK(text.find("\"channel\": \"color\"") != std::string::npos);
  CHECK(text.find("ssim") != std::string::npos);
  CHECK(text.find("results/geometry/sphere/1_soup.color.png")
      != std::string::npos);

  // schema v2: the producing device names the run.
  CHECK(text.find("\"device\"") != std::string::npos);
  CHECK(text.find("\"library\": \"helide\"") != std::string::npos);
  CHECK(text.find("\"renderer\": \"default\"") != std::string::npos);

  // Optional debug image paths round-trip when present.
  CHECK(text.find("1_soup.color.diff.png") != std::string::npos);
  CHECK(text.find("1_soup.color.threshold.png") != std::string::npos);
}

TEST_CASE("device defaults serialize when unset", "[cts][sidecar]")
{
  CaseResult r;
  r.verdict = Verdict::Skipped;
  const std::string text = toJson(r);
  // Always present so the Python diff can rely on the field (v2); the library
  // is empty for a default-constructed spec, device/renderer default.
  CHECK(text.find("\"device\"") != std::string::npos);
  CHECK(text.find("\"device\": \"default\"") != std::string::npos);
}

TEST_CASE(
    "sidecar carries a behavioral case's verdict and detail", "[cts][sidecar]")
{
  CaseResult r;
  r.category = "frame";
  r.test = "frame_completion_callback";
  r.caseId = "default";
  r.verdict = Verdict::Passed;
  r.detail = "frame completion callback fired";
  // Behavioral cases have no channels.

  const std::string text = toJson(r);
  CHECK(text.find("\"verdict\": \"passed\"") != std::string::npos);
  CHECK(text.find("\"detail\": \"frame completion callback fired\"")
      != std::string::npos);
  CHECK(text.find("\"channels\": []") != std::string::npos);
}

TEST_CASE("non-finite scores serialize as JSON null", "[cts][sidecar]")
{
  CaseResult r;
  r.verdict = Verdict::Passed;
  ChannelResult ch;
  ch.metrics = {{"psnr", std::numeric_limits<double>::infinity()}};
  ch.passed = true;
  r.channels.push_back(ch);

  const std::string text = toJson(r);
  CHECK(text.find("\"psnr\": null") != std::string::npos);
}

TEST_CASE(
    "writeSidecar creates directories and a readable file", "[cts][sidecar]")
{
  const auto dir =
      std::filesystem::temp_directory_path() / "cts_sidecar_test" / "nested";
  std::error_code ec;
  std::filesystem::remove_all(dir.parent_path(), ec);

  const auto path = dir / "case.json";
  CaseResult r;
  r.category = "light";
  r.test = "directional";
  r.caseId = "default";
  r.verdict = Verdict::Skipped;
  r.skipReason = "missing feature ANARI_KHR_LIGHT_DIRECTIONAL";

  REQUIRE(writeSidecar(path, r));
  REQUIRE(std::filesystem::exists(path));

  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  const std::string contents = buffer.str();
  CHECK(contents.find("\"verdict\": \"skipped\"") != std::string::npos);
  CHECK(contents.find("missing feature") != std::string::npos);
  in.close();

  r.verdict = Verdict::Passed;
  r.skipReason.clear();
  REQUIRE(writeSidecar(path, r));
  std::ifstream replacement(path);
  std::stringstream replacementBuffer;
  replacementBuffer << replacement.rdbuf();
  CHECK(replacementBuffer.str().find("\"verdict\": \"passed\"")
      != std::string::npos);
  replacement.close();

  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    CHECK(entry.path().filename() == path.filename());
  }

  std::filesystem::remove_all(dir.parent_path(), ec);
}

// Artifact publication ///////////////////////////////////////////////////////

TEST_CASE("ArtifactPublisher replaces a complete ground-truth set",
    "[cts][publication]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_gt_publication_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const Workdir wd(root);
  ArtifactPublisher publisher(wd);
  const auto colorPath = wd.groundTruthImagePath(sphereCase(), Channel::Color);
  const auto depthPath = wd.groundTruthImagePath(sphereCase(), Channel::Depth);

  REQUIRE(publisher.publishGroundTruth({{colorPath, solidImage(255, 0, 0)},
      {depthPath, solidImage(0, 0, 255)}}));
  CHECK(loadPNG(colorPath.string()).rgba == solidImage(255, 0, 0).rgba);
  CHECK(loadPNG(depthPath.string()).rgba == solidImage(0, 0, 255).rgba);

  REQUIRE(publisher.publishGroundTruth({{colorPath, solidImage(0, 255, 0)},
      {depthPath, solidImage(255, 255, 255)}}));
  CHECK(loadPNG(colorPath.string()).rgba == solidImage(0, 255, 0).rgba);
  CHECK(loadPNG(depthPath.string()).rgba == solidImage(255, 255, 255).rgba);

  std::filesystem::remove_all(root, ec);
}

TEST_CASE(
    "ArtifactPublisher withholds a sidecar until every referenced image "
    "is readable",
    "[cts][publication]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "cts_result_publication_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const Case c = sphereCase();
  const Workdir wd(root);
  ArtifactPublisher publisher(wd);

  CaseResult result;
  result.category = c.category;
  result.test = c.testName;
  result.caseId = c.id();
  result.groundTruthKey = c.groundTruthKey();
  result.verdict = Verdict::Passed;
  ChannelResult channel;
  channel.channel = Channel::Color;
  channel.passed = true;
  channel.resultImage =
      wd.relativeToRoot(wd.resultImagePath(c, Channel::Color));
  channel.groundTruthImage =
      wd.relativeToRoot(wd.groundTruthImagePath(c, Channel::Color));
  result.channels.push_back(channel);

  CHECK_FALSE(publisher.publishResult(c,
      result,
      {{wd.resultImagePath(c, Channel::Color), solidImage(255, 0, 0)}}));
  CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(c)));
  CHECK_FALSE(std::filesystem::exists(wd.resultImagePath(c, Channel::Color)));

  std::filesystem::remove_all(root, ec);
}
