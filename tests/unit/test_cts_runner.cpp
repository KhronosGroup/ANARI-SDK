// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts
#include "cts/Catalog.h"
#include "cts/Runner.h"
#include "cts/TestBuilder.h"
#include "cts/WorldBuilder.h"
// std
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

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
  GeometryOptions opts;
  opts.subtype = "triangle";
  opts.primitiveCount = ctx.get<int>("primitiveCount", 4);
  opts.primitiveMode = ctx.getString("primitiveMode", "soup");
  opts.seed = 11;
  auto geom = buildGeometry(d, opts);
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
  GeometryOptions opts;
  opts.subtype = "sphere";
  opts.primitiveCount = 6;
  opts.seed = 3;
  auto geom = buildGeometry(d, opts);
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
      .build(buildTriangleWorld)
      .permute("primitiveCount", {4, 8})
      .variant("primitiveMode", {"soup", "indexed"})
      .registerInto(cat);
  // Single case, two channels.
  makeTest("geometry", "sphere")
      .build(buildSphereWorld)
      .channels({Channel::Color, Channel::Depth})
      .registerInto(cat);
  // Requires a feature no device has -> always skipped.
  makeTest("demo", "needs_bogus")
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

} // namespace

TEST_CASE("Runner generates ground truth then scores a run", "[cts][runner][helide]")
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
  const std::set<std::string> features; // no real features required by the tests
  RunOptions opts;
  opts.width = 64;
  opts.height = 64;

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
    CHECK(std::filesystem::exists(wd.groundTruthImagePath(tri, Channel::Color)));

    Case sph;
    sph.category = "geometry";
    sph.testName = "sphere";
    CHECK(std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Color)));
    CHECK(std::filesystem::exists(wd.groundTruthImagePath(sph, Channel::Depth)));

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
    CHECK(text.find("ssim") != std::string::npos);
    CHECK(std::filesystem::exists(wd.resultImagePath(tri, Channel::Color)));

    // The bogus-feature test is skipped with a feature reason.
    Case bogus;
    bogus.category = "demo";
    bogus.testName = "needs_bogus";
    const auto bogusText = readFile(wd.sidecarPath(bogus));
    CHECK(bogusText.find("\"verdict\": \"skipped\"") != std::string::npos);
    CHECK(bogusText.find("feature") != std::string::npos);
  }

  std::filesystem::remove_all(root, ec);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
