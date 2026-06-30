// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/Case.h"
#include "cts/Catalog.h"
#include "cts/FrameFormats.h"
#include "cts/FrameReadback.h"
#include "cts/GeometryBuilder.h"
#include "cts/LightBuilder.h"
#include "cts/Runner.h"
#include "cts/SurfaceBuilder.h"
#include "cts/TestBuilder.h"
#include "cts/WorldBuilder.h"
// std
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace anari::cts;
using anari::math::float3;

namespace {

void statusFunc(const void *,
    anari::Device,
    anari::Object,
    anari::DataType,
    anari::StatusSeverity,
    anari::StatusCode,
    const char *)
{}

// A Test build function: read axis values from the context and author a world.
anari::World buildTriangleWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = ctx.get<int>("primitiveCount", 4);
  spec.mode = parsePrimitiveMode(ctx.getString("primitiveMode", "soup"));
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.8f, 0.4f, 0.2f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

anari::World buildSphereWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  SphereSpec spec;
  spec.primitiveCount = 6;
  auto geom = buildSphereGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.3f, 0.7f, 0.5f));
  auto surface = makeSurface(d, geom, mat);
  auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
  WorldContents wc;
  wc.surfaces = {surface};
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  anari::release(d, light);
  return world;
}

Catalog makeCatalog()
{
  Catalog cat;
  // 2 permutations x 2 variants = 4 cases, 2 ground-truth keys.
  makeTest("geometry", "triangle")
      .description("Checks triangle primitive layouts.")
      .build(buildTriangleWorld)
      .permute("primitiveCount", {4, 8})
      .variant("primitiveMode", {"soup", "indexed"})
      .registerInto(cat);
  // Single case, two channels.
  makeTest("geometry", "sphere")
      .description("Checks sphere color and depth rendering.")
      .build(buildSphereWorld)
      .channels({Channel::Color, Channel::Depth})
      .registerInto(cat);
  // Requires a feature no device has -> always skipped.
  makeTest("demo", "needs_bogus")
      .description("Checks feature-gated Test reporting.")
      .build(buildSphereWorld)
      .requireFeature("ANARI_KHR_BOGUS_FEATURE")
      .registerInto(cat);
  return cat;
}

std::string readFile(const std::filesystem::path &p)
{
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool hasPublicationTemporary(const std::filesystem::path &root)
{
  if (!std::filesystem::exists(root))
    return false;
  for (const auto &entry :
      std::filesystem::recursive_directory_iterator(root)) {
    const auto name = entry.path().filename().string();
    if (name.find(".stage.") != std::string::npos
        || name.find(".backup.") != std::string::npos)
      return true;
  }
  return false;
}

struct FailingArtifactWriter : public ArtifactWriter
{
  int failImageWrite{-1};
  bool failSidecarWrite{false};

  bool writeImage(
      const std::filesystem::path &path, const Image &image) override
  {
    if (m_imageWrites++ == failImageWrite)
      return false;
    return savePNG(path.string(), image);
  }

  bool writeSidecar(
      const std::filesystem::path &path, const CaseResult &result) override
  {
    return !failSidecarWrite && anari::cts::writeSidecar(path, result);
  }

 private:
  int m_imageWrites{0};
};

// A Case carrying a single output-format axis value.
Case caseWithFormat(const std::string &axis, anari::cts::Any value)
{
  Case c;
  c.category = "frame";
  c.testName = "fmt";
  c.values = {{axis, std::move(value), AxisKind::Permutation}};
  return c;
}

} // namespace

// Frame output-format resolution (pure; no device) ////////////////////////////

TEST_CASE("frame readback rejects null mapped data", "[cts][framereadback]")
{
  const MappedFrameDescriptor mapped{nullptr, 2, 2, ANARI_UFIXED8_RGBA_SRGB};

  const auto result = decodeFrameChannel(Channel::Color, mapped, 2, 2);

  CHECK_FALSE(result);
  CHECK(result.error == FrameReadbackError::NullData);
  CHECK(result.detail.find("channel.color") != std::string::npos);
  CHECK(result.detail.find("null") != std::string::npos);
}

TEST_CASE(
    "frame readback requires exact mapped dimensions", "[cts][framereadback]")
{
  const uint32_t pixel = 0;

  SECTION("short dimensions")
  {
    const MappedFrameDescriptor mapped{&pixel, 1, 2, ANARI_UFIXED8_RGBA_SRGB};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 2, 2);

    CHECK_FALSE(result);
    CHECK(result.error == FrameReadbackError::DimensionMismatch);
    CHECK(result.detail.find("expected 2x2") != std::string::npos);
    CHECK(result.detail.find("got 1x2") != std::string::npos);
  }

  SECTION("oversized dimensions")
  {
    const MappedFrameDescriptor mapped{&pixel, 3, 2, ANARI_UFIXED8_RGBA_SRGB};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 2, 2);

    CHECK_FALSE(result);
    CHECK(result.error == FrameReadbackError::DimensionMismatch);
    CHECK(result.detail.find("expected 2x2") != std::string::npos);
    CHECK(result.detail.find("got 3x2") != std::string::npos);
  }
}

TEST_CASE("frame readback rejects unsupported types for every channel",
    "[cts][framereadback]")
{
  const uint32_t pixel = 0;
  const std::pair<Channel, ANARIDataType> cases[] = {
      {Channel::Color, ANARI_FLOAT32},
      {Channel::Depth, ANARI_UINT32},
      {Channel::Albedo, ANARI_FIXED16_VEC3},
      {Channel::Normal, ANARI_UFIXED8_VEC3},
      {Channel::PrimitiveId, ANARI_FLOAT32},
      {Channel::ObjectId, ANARI_FLOAT32},
      {Channel::InstanceId, ANARI_FLOAT32},
  };

  for (const auto &[channel, pixelType] : cases) {
    INFO("channel: " << channelName(channel));
    const MappedFrameDescriptor mapped{&pixel, 1, 1, pixelType};
    const auto result = decodeFrameChannel(channel, mapped, 1, 1);

    CHECK_FALSE(result);
    CHECK(result.error == FrameReadbackError::UnsupportedType);
    CHECK(result.detail.find(channelName(channel)) != std::string::npos);
    CHECK(result.detail.find(anari::toString(pixelType)) != std::string::npos);
  }
}

TEST_CASE(
    "frame readback rejects overflow-sized dimensions", "[cts][framereadback]")
{
  const uint32_t pixel = 0;
  constexpr uint32_t huge = std::numeric_limits<uint32_t>::max();
  const MappedFrameDescriptor mapped{
      &pixel, huge, huge, ANARI_UFIXED8_RGBA_SRGB};

  const auto result = decodeFrameChannel(Channel::Color, mapped, huge, huge);

  CHECK_FALSE(result);
  CHECK(result.error == FrameReadbackError::SizeOverflow);
  CHECK(result.detail.find("overflow") != std::string::npos);
}

TEST_CASE("frame readback decodes every supported color format",
    "[cts][framereadback]")
{
  SECTION("UFIXED8_VEC4")
  {
    const uint8_t pixel[] = {10, 20, 30, 40};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_VEC4};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{10, 20, 30, 40});
  }

  SECTION("UFIXED8_RGBA_SRGB")
  {
    const uint8_t pixel[] = {50, 60, 70, 80};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_RGBA_SRGB};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{50, 60, 70, 80});
  }

  SECTION("FLOAT32_VEC4")
  {
    const float pixel[] = {0.f, 0.25f, 0.5f, 1.f};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FLOAT32_VEC4};
    const auto result = decodeFrameChannel(Channel::Color, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{0, 63, 127, 255});
  }
}

TEST_CASE(
    "frame readback decodes depth with the scene scale", "[cts][framereadback]")
{
  const float pixels[] = {0.f, 2.f, std::numeric_limits<float>::infinity()};
  const MappedFrameDescriptor mapped{pixels, 3, 1, ANARI_FLOAT32};

  const auto result = decodeFrameChannel(Channel::Depth, mapped, 3, 1, 4.f);

  REQUIRE(result);
  CHECK(result.image.rgba
      == std::vector<uint8_t>{
          0, 0, 0, 255, 127, 127, 127, 255, 255, 255, 255, 255});
}

TEST_CASE("frame readback decodes every supported albedo format",
    "[cts][framereadback]")
{
  SECTION("UFIXED8_VEC3")
  {
    const uint8_t pixel[] = {10, 20, 30};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_VEC3};
    const auto result = decodeFrameChannel(Channel::Albedo, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{10, 20, 30, 255});
  }

  SECTION("UFIXED8_RGB_SRGB")
  {
    const uint8_t pixel[] = {40, 50, 60};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_UFIXED8_RGB_SRGB};
    const auto result = decodeFrameChannel(Channel::Albedo, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{40, 50, 60, 255});
  }

  SECTION("FLOAT32_VEC3")
  {
    const float pixel[] = {0.25f, 0.5f, 1.f};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FLOAT32_VEC3};
    const auto result = decodeFrameChannel(Channel::Albedo, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{63, 127, 255, 255});
  }
}

TEST_CASE("frame readback decodes every supported normal format",
    "[cts][framereadback]")
{
  SECTION("FIXED16_VEC3")
  {
    const int16_t pixel[] = {-32768, 0, 32767};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FIXED16_VEC3};
    const auto result = decodeFrameChannel(Channel::Normal, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{0, 127, 255, 255});
  }

  SECTION("FLOAT32_VEC3")
  {
    const float pixel[] = {-1.f, 0.f, 1.f};
    const MappedFrameDescriptor mapped{pixel, 1, 1, ANARI_FLOAT32_VEC3};
    const auto result = decodeFrameChannel(Channel::Normal, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{0, 127, 255, 255});
  }
}

TEST_CASE("frame readback decodes every ID channel", "[cts][framereadback]")
{
  const uint32_t pixel = 0;
  const Channel channels[] = {
      Channel::PrimitiveId, Channel::ObjectId, Channel::InstanceId};

  for (const Channel channel : channels) {
    INFO("channel: " << channelName(channel));
    const MappedFrameDescriptor mapped{&pixel, 1, 1, ANARI_UINT32};
    const auto result = decodeFrameChannel(channel, mapped, 1, 1);

    REQUIRE(result);
    CHECK(result.image.rgba == std::vector<uint8_t>{140, 209, 198, 255});
  }
}

TEST_CASE("output-format strings map to ANARIDataType", "[cts][frameformat]")
{
  CHECK(colorFormatFromString("UFIXED8_RGBA_SRGB") == ANARI_UFIXED8_RGBA_SRGB);
  CHECK(colorFormatFromString("UFIXED8_VEC4") == ANARI_UFIXED8_VEC4);
  CHECK(colorFormatFromString("FLOAT32_VEC4") == ANARI_FLOAT32_VEC4);
  CHECK(colorFormatFromString("nonsense") == ANARI_UNKNOWN);

  CHECK(albedoFormatFromString("UFIXED8_VEC3") == ANARI_UFIXED8_VEC3);
  CHECK(albedoFormatFromString("UFIXED8_RGB_SRGB") == ANARI_UFIXED8_RGB_SRGB);
  CHECK(albedoFormatFromString("FLOAT32_VEC3") == ANARI_FLOAT32_VEC3);
  CHECK(
      albedoFormatFromString("FIXED16_VEC3") == ANARI_UNKNOWN); // wrong channel

  CHECK(normalFormatFromString("FIXED16_VEC3") == ANARI_FIXED16_VEC3);
  CHECK(normalFormatFromString("FLOAT32_VEC3") == ANARI_FLOAT32_VEC3);
  CHECK(
      normalFormatFromString("UFIXED8_VEC3") == ANARI_UNKNOWN); // wrong channel
}

TEST_CASE("channels have fixed default formats", "[cts][frameformat]")
{
  CHECK(channelDefaultFormat(Channel::Color) == ANARI_UFIXED8_RGBA_SRGB);
  CHECK(channelDefaultFormat(Channel::Depth) == ANARI_FLOAT32);
  CHECK(channelDefaultFormat(Channel::Albedo) == ANARI_FLOAT32_VEC3);
  CHECK(channelDefaultFormat(Channel::Normal) == ANARI_FLOAT32_VEC3);
  CHECK(channelDefaultFormat(Channel::PrimitiveId) == ANARI_UINT32);
}

TEST_CASE("caseChannelFormat honors the output-format axis per channel",
    "[cts][frameformat]")
{
  // The color axis overrides the color channel.
  auto colorCase = caseWithFormat("frame_color_type", Any("FLOAT32_VEC4"));
  CHECK(caseChannelFormat(colorCase, Channel::Color) == ANARI_FLOAT32_VEC4);
  // ...but does not bleed into other channels.
  CHECK(caseChannelFormat(colorCase, Channel::Depth) == ANARI_FLOAT32);

  // Albedo and normal axes drive their own channels (D2).
  auto albedoCase = caseWithFormat("frame_albedo_type", Any("UFIXED8_VEC3"));
  CHECK(caseChannelFormat(albedoCase, Channel::Albedo) == ANARI_UFIXED8_VEC3);

  auto normalCase = caseWithFormat("frame_normal_type", Any("FIXED16_VEC3"));
  CHECK(caseChannelFormat(normalCase, Channel::Normal) == ANARI_FIXED16_VEC3);

  // No axis -> the channel default.
  Case bare;
  CHECK(caseChannelFormat(bare, Channel::Albedo) == ANARI_FLOAT32_VEC3);
  CHECK(caseChannelFormat(bare, Channel::Color) == ANARI_UFIXED8_RGBA_SRGB);

  // An unrecognized axis value falls back to the channel default.
  auto bogus = caseWithFormat("frame_color_type", Any("BOGUS"));
  CHECK(caseChannelFormat(bogus, Channel::Color) == ANARI_UFIXED8_RGBA_SRGB);
}

TEST_CASE(
    "Runner generates ground truth then scores a run", "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping runner integration test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root = std::filesystem::temp_directory_path() / "cts_runner_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  auto catalog = makeCatalog();
  const std::set<std::string>
      features; // no real features required by the tests
  RunOptions opts;
  opts.width = 64;
  opts.height = 64;
  opts.device = {"helide", "default", "default"};

  Runner runner(d, Workdir(root), opts);

  SECTION("run before generate skips for lack of ground truth")
  {
    auto summary = runner.run(catalog, Filter{"geometry"}, features);
    CHECK(summary.total == 5); // 4 triangle + 1 sphere
    CHECK(summary.skipped == 5);
    CHECK(summary.passed == 0);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const auto sidecar = Workdir(root).sidecarPath(sph);
    REQUIRE(std::filesystem::exists(sidecar));
    const auto text = readFile(sidecar);
    CHECK(text.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(text.find("ground truth") != std::string::npos);
  }

  SECTION("generate then run passes against its own output")
  {
    auto genSummary = runner.generate(catalog, Filter{""}, features);
    CHECK(genSummary.total == 6); // 4 + 1 + 1(bogus)
    CHECK(genSummary.passed == 5); // bogus is skipped
    CHECK(genSummary.skipped == 1);

    // ADR-0005: the generated workdir ignores itself.
    CHECK(std::filesystem::exists(root / ".gitignore"));

    // Ground-truth images exist, keyed by the shared ground-truth key.
    Workdir wd(root);
    Case tri;
    tri.category = "geometry";
    tri.testName = "triangle";
    tri.values = {{"primitiveCount", Any(4), AxisKind::Permutation},
        {"primitiveMode", Any("soup"), AxisKind::Variant}};
    CHECK(
        std::filesystem::exists(wd.groundTruthImagePath(tri, Channel::Color)));

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    CHECK(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Color)));
    CHECK(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Depth)));

    // A candidate identical to the reference (both helide) must pass.
    auto runSummary = runner.run(catalog, Filter{""}, features);
    CHECK(runSummary.total == 6);
    CHECK(runSummary.passed == 5);
    CHECK(runSummary.skipped == 1); // the bogus-feature test

    // Sidecar + result image for a triangle case.
    const auto sidecar = wd.sidecarPath(tri);
    REQUIRE(std::filesystem::exists(sidecar));
    const auto text = readFile(sidecar);
    CHECK(text.find("\"verdict\": \"passed\"") != std::string::npos);
    CHECK(text.find("\"description\": \"Checks triangle primitive layouts.\"")
        != std::string::npos);
    CHECK(text.find("ssim") != std::string::npos);
    CHECK(std::filesystem::exists(wd.resultImagePath(tri, Channel::Color)));
    // The run records its device identity (schema v2) and emits debug images.
    CHECK(text.find("\"library\": \"helide\"") != std::string::npos);
    CHECK(std::filesystem::exists(wd.diffImagePath(tri, Channel::Color)));
    CHECK(std::filesystem::exists(wd.thresholdImagePath(tri, Channel::Color)));

    // The bogus-feature test is skipped with a feature reason.
    Case bogus;
    bogus.category = "demo";
    bogus.testName = "needs_bogus";
    const auto bogusText = readFile(wd.sidecarPath(bogus));
    CHECK(bogusText.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(bogusText.find(
              "\"description\": \"Checks feature-gated Test reporting.\"")
        != std::string::npos);
    CHECK(bogusText.find("feature") != std::string::npos);
  }

  SECTION("a result directory creation failure fails the Case")
  {
    auto genSummary = runner.generate(catalog, Filter{"geometry"}, features);
    REQUIRE(genSummary.passed == 5);

    const auto blockedDirectory = root / "results" / "geometry" / "sphere";
    std::filesystem::create_directories(blockedDirectory.parent_path());
    {
      std::ofstream blocker(blockedDirectory);
      REQUIRE(blocker);
      blocker << "not a directory\n";
    }

    const auto runSummary = runner.run(catalog, Filter{"geometry"}, features);
    CHECK(runSummary.total == 5);
    CHECK(runSummary.passed == 4);
    CHECK(runSummary.failed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    CHECK_FALSE(std::filesystem::exists(Workdir(root).sidecarPath(sph)));
  }

  SECTION("a workdir setup failure fails every selected Case")
  {
    {
      std::ofstream blocker(root);
      REQUIRE(blocker);
      blocker << "not a directory\n";
    }

    const auto summary = runner.generate(catalog, Filter{"demo"}, features);
    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK(summary.skipped == 0);
  }

  SECTION("an image write failure cannot leave a stale passing sidecar")
  {
    REQUIRE(runner.generate(catalog, Filter{"sphere"}, features).passed == 1);
    REQUIRE(runner.run(catalog, Filter{"sphere"}, features).passed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    REQUIRE(std::filesystem::exists(wd.sidecarPath(sph)));
    const auto previousImage =
        readFile(wd.resultImagePath(sph, Channel::Color));

    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failImageWrite = 0;
    Runner failingRunner(d, wd, opts, writer);
    const auto summary = failingRunner.run(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(sph)));
    CHECK(readFile(wd.resultImagePath(sph, Channel::Color)) == previousImage);
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  SECTION("a sidecar write failure fails the Case and preserves its images")
  {
    REQUIRE(runner.generate(catalog, Filter{"sphere"}, features).passed == 1);
    REQUIRE(runner.run(catalog, Filter{"sphere"}, features).passed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    const auto previousImage =
        readFile(wd.resultImagePath(sph, Channel::Color));

    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failSidecarWrite = true;
    Runner failingRunner(d, wd, opts, writer);
    const auto summary = failingRunner.run(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK_FALSE(std::filesystem::exists(wd.sidecarPath(sph)));
    CHECK(readFile(wd.resultImagePath(sph, Channel::Color)) == previousImage);
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  SECTION("partial multi-channel ground truth is not published")
  {
    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failImageWrite = 1;
    Runner failingRunner(d, Workdir(root), opts, writer);
    const auto summary =
        failingRunner.generate(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    CHECK_FALSE(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Color)));
    CHECK_FALSE(
        std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Depth)));
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  SECTION("failed ground-truth replacement preserves the complete old set")
  {
    REQUIRE(runner.generate(catalog, Filter{"sphere"}, features).passed == 1);

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const Workdir wd(root);
    const auto previousColor =
        readFile(wd.groundTruthImagePath(sph, Channel::Color));
    const auto previousDepth =
        readFile(wd.groundTruthImagePath(sph, Channel::Depth));

    auto writer = std::make_shared<FailingArtifactWriter>();
    writer->failImageWrite = 1;
    Runner failingRunner(d, wd, opts, writer);
    const auto summary =
        failingRunner.generate(catalog, Filter{"sphere"}, features);

    CHECK(summary.total == 1);
    CHECK(summary.passed == 0);
    CHECK(summary.failed == 1);
    CHECK(readFile(wd.groundTruthImagePath(sph, Channel::Color))
        == previousColor);
    CHECK(readFile(wd.groundTruthImagePath(sph, Channel::Depth))
        == previousDepth);
    CHECK_FALSE(hasPublicationTemporary(root));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE(
    "accumulation gates off when unsupported but denoise is applied "
    "unconditionally; the sidecar records the effective settings",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping accumulation/denoise test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_accum_denoise_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog cat;
  makeTest("geometry", "sphere").build(buildSphereWorld).registerInto(cat);

  // Request accumulation + denoise, but helide advertises neither extension.
  const std::set<std::string> features; // helide: no accumulation, no denoise
  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  opts.accumulationFrames = 16; // CLI default; gated off here (no extension)
  // Applied even though helide lacks the extension.
  opts.denoise = true;
  opts.device = {"helide", "default", "default"};

  Runner runner(d, Workdir(root), opts);

  // Ground truth then a run. Accumulation gates off (-> 1). Denoise is set on
  // the renderer regardless, but helide ignores the unknown param, so a
  // candidate identical to its own ground truth still passes.
  auto gen = runner.generate(cat, Filter{""}, features);
  CHECK(gen.passed == 1);
  auto s = runner.run(cat, Filter{""}, features);
  CHECK(s.total == 1);
  CHECK(s.passed == 1);
  CHECK(s.failed == 0);

  // The sidecar detail records the effective settings: accumulation resolved to
  // 1 (gated off, no extension) but denoise stays on (applied unconditionally).
  Case sph;
  sph.category = "geometry";
  sph.testName = "sphere";
  const auto text = readFile(Workdir(root).sidecarPath(sph));
  CHECK(text.find("accumulation=1, denoise=on") != std::string::npos);

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("Runner isolates a throwing case and completes the rest",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping crash-isolation test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root = std::filesystem::temp_directory_path() / "cts_crash_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  // A build hook that throws only when the shared flag is set, so we can
  // generate clean ground truth first and then make the same Case explode.
  auto shouldThrow = std::make_shared<bool>(false);
  Catalog cat;
  makeTest("geometry", "triangle").build(buildTriangleWorld).registerInto(cat);
  makeTest("geometry", "explodes")
      .build([shouldThrow](BuildContext &ctx) -> anari::World {
        if (*shouldThrow)
          throw std::runtime_error("boom during build");
        return buildSphereWorld(ctx);
      })
      .registerInto(cat);
  makeTest("geometry", "sphere").build(buildSphereWorld).registerInto(cat);

  const std::set<std::string> features;
  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  Runner runner(d, Workdir(root), opts);

  Case explodes;
  explodes.category = "geometry";
  explodes.testName = "explodes";

  SECTION("a throw during run() yields one failed sidecar; the run continues")
  {
    // Ground truth for every Case (nothing throws yet).
    auto gen = runner.generate(cat, Filter{""}, features);
    CHECK(gen.total == 3);
    CHECK(gen.passed == 3);

    // Now make the middle Case throw during its build.
    *shouldThrow = true;
    auto s = runner.run(cat, Filter{""}, features);
    CHECK(s.total == 3);
    CHECK(s.passed == 2); // triangle + sphere still scored
    CHECK(s.failed == 1); // explodes
    CHECK(s.skipped == 0);

    // The throwing Case is recorded as failed with the message in detail.
    const auto text = readFile(Workdir(root).sidecarPath(explodes));
    CHECK(text.find("\"verdict\": \"failed\"") != std::string::npos);
    CHECK(text.find("boom during build") != std::string::npos);

    // The cases after the throwing one still produced their sidecars.
    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    const auto sphText = readFile(Workdir(root).sidecarPath(sph));
    CHECK(sphText.find("\"verdict\": \"passed\"") != std::string::npos);
  }

  SECTION("a throw during generate() loses only that case")
  {
    *shouldThrow = true;
    auto gen = runner.generate(cat, Filter{""}, features);
    CHECK(gen.total == 3);
    CHECK(gen.passed == 2); // triangle + sphere ground truth produced
    CHECK(gen.failed == 1); // explodes

    // The surrounding cases' ground truth exists despite the throw.
    Case tri;
    tri.category = "geometry";
    tri.testName = "triangle";
    CHECK(std::filesystem::exists(
        Workdir(root).groundTruthImagePath(tri, Channel::Color)));
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

TEST_CASE("Runner verifies behavioral tests via the behavior hook",
    "[cts][runner][helide]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (!lib) {
    WARN("helide library not available; skipping behavior hook test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  const auto root =
      std::filesystem::temp_directory_path() / "cts_behavior_test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  Catalog cat;
  // A behavior check that inspects what the runner built and passes.
  makeTest("frame", "behaves")
      .build(buildTriangleWorld)
      .behavior([](anari::Device dev,
                    anari::World w,
                    anari::Camera cam,
                    anari::Renderer r,
                    uint32_t width,
                    uint32_t height) -> BehaviorResult {
        const bool ok = dev != nullptr && w != nullptr && cam != nullptr
            && r != nullptr && width > 0 && height > 0;
        return {ok, ok ? "scene objects present" : "missing scene objects"};
      })
      .registerInto(cat);
  // A behavior check gated on a feature no device has -> skipped, never called.
  makeTest("frame", "behaves_bogus")
      .build(buildTriangleWorld)
      .behavior(
          [](anari::Device,
              anari::World,
              anari::Camera,
              anari::Renderer,
              uint32_t,
              uint32_t) -> BehaviorResult { return {false, "should not run"}; })
      .requireFeature("ANARI_KHR_BOGUS_FEATURE")
      .registerInto(cat);

  RunOptions opts;
  opts.width = 32;
  opts.height = 32;
  Runner runner(d, Workdir(root), opts);

  SECTION("generate skips behavioral tests (no ground truth)")
  {
    auto g = runner.generate(cat, Filter{""}, {});
    CHECK(g.total == 2);
    CHECK(g.skipped == 2);
    CHECK(g.passed == 0);
    // No ground truth produced for a behavioral test.
    Case behaves;
    behaves.category = "frame";
    behaves.testName = "behaves";
    CHECK_FALSE(std::filesystem::exists(
        Workdir(root).groundTruthImagePath(behaves, Channel::Color)));
  }

  SECTION("run invokes the hook and records verdict + detail")
  {
    auto s = runner.run(cat, Filter{""}, {});
    CHECK(s.total == 2);
    CHECK(s.passed == 1);
    CHECK(s.skipped == 1);
    CHECK(s.failed == 0);

    Case behaves;
    behaves.category = "frame";
    behaves.testName = "behaves";
    const auto text = readFile(Workdir(root).sidecarPath(behaves));
    CHECK(text.find("\"verdict\": \"passed\"") != std::string::npos);
    CHECK(text.find("scene objects present") != std::string::npos);
    // No images are rendered for a behavioral case.
    CHECK_FALSE(std::filesystem::exists(
        Workdir(root).resultImagePath(behaves, Channel::Color)));

    Case bogus;
    bogus.category = "frame";
    bogus.testName = "behaves_bogus";
    const auto bogusText = readFile(Workdir(root).sidecarPath(bogus));
    CHECK(bogusText.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(bogusText.find("feature") != std::string::npos);
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
