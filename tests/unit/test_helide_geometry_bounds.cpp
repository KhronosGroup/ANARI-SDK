// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regression for helide's sphere and curve array bounds checks: out-of-range
// 'primitive.index' values clamp, a short 'vertex.radius' falls back to
// 'radius', a curve with fewer than 2 vertices has no segments, and UINT64
// 'primitive.index' arrays are converted. Each malformed case warns and
// renders the same image as its well-formed equivalent.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <string>
#include <vector>

namespace {

using namespace helide_test;

// A world holding one surface with geometry 'geom' (released here).
anari::World makeWorld(anari::Device d, anari::Geometry geom)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(0.8f));
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// Renders a world holding only 'geom' (committed and released here).
RenderResult renderGeometry(anari::Device d, anari::Geometry geom)
{
  anari::commitParameters(d, geom);
  auto world = makeWorld(d, geom);
  auto result = renderChannels(d, world);
  anari::release(d, world);
  return result;
}

// Whether 'r' differs from a render of an empty world, i.e. hit something.
bool hitSomething(anari::Device d, const RenderResult &r)
{
  auto world = anari::newObject<anari::World>(d);
  anari::commitParameters(d, world);
  const auto empty = renderChannels(d, world);
  anari::release(d, world);
  return countDepthMismatches(r.depth, empty.depth) > 0;
}

const std::vector<float3> kSpherePositions = {
    {-0.5f, 0.f, 0.f}, {0.f, 0.3f, 0.f}, {0.5f, 0.f, 0.f}};

const std::vector<float3> kCurvePositions = {{-0.6f, -0.3f, 0.f},
    {-0.2f, 0.3f, 0.f},
    {0.2f, -0.3f, 0.f},
    {0.6f, 0.3f, 0.f}};

anari::Geometry newSpheres(anari::Device d)
{
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d,
      geom,
      "vertex.position",
      kSpherePositions.data(),
      kSpherePositions.size());
  anari::setParameter(d, geom, "radius", 0.15f);
  return geom;
}

anari::Geometry newCurve(
    anari::Device d, const std::vector<float3> &positions = kCurvePositions)
{
  auto geom = anari::newObject<anari::Geometry>(d, "curve");
  if (!positions.empty()) {
    anari::setParameterArray1D(
        d, geom, "vertex.position", positions.data(), positions.size());
  } else {
    anari::setParameterArray1D(
        d, geom, "vertex.position", (const float3 *)nullptr, 0);
  }
  anari::setParameter(d, geom, "radius", 0.05f);
  return geom;
}

} // namespace

TEST_CASE("helide sphere and curve arrays are bounds-checked",
    "[helide][helide_geometry_bounds]")
{
  WarningLog warnings;
  anari::Library lib = anari::loadLibrary("helide", collectWarnings, &warnings);
  if (lib == nullptr) {
    WARN("helide library not available; skipping geometry bounds test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  SECTION("sphere 'primitive.index' past the vertices clamps to the last")
  {
    const std::vector<uint32_t> expectedIndex = {0, 2, 2};
    auto expected = newSpheres(d);
    anari::setParameterArray1D(
        d, expected, "primitive.index", expectedIndex.data(), 3);
    const auto want = renderGeometry(d, expected);
    REQUIRE(warnings.empty());

    const std::vector<uint32_t> index = {0, 7, 1000000};
    auto geom = newSpheres(d);
    anari::setParameterArray1D(d, geom, "primitive.index", index.data(), 3);
    const auto got = renderGeometry(d, geom);
    CHECK(warnings.contains("primitive.index"));
    CHECK(hitSomething(d, got));
    checkSameImage(got, want);
  }

  SECTION("sphere 'vertex.radius' shorter than the vertices uses 'radius'")
  {
    const std::vector<float> fullRadius = {0.25f, 0.15f, 0.15f};
    auto expected = newSpheres(d);
    anari::setParameterArray1D(
        d, expected, "vertex.radius", fullRadius.data(), 3);
    const auto want = renderGeometry(d, expected);
    REQUIRE(warnings.empty());

    const float shortRadius = 0.25f;
    auto geom = newSpheres(d);
    anari::setParameterArray1D(d, geom, "vertex.radius", &shortRadius, 1);
    const auto got = renderGeometry(d, geom);
    CHECK(warnings.contains("vertex.radius"));
    checkSameImage(got, want);

    SECTION("also when indexed")
    {
      warnings.clear();
      const std::vector<uint32_t> index = {2, 1, 0};
      auto indexed = newSpheres(d);
      anari::setParameterArray1D(d, indexed, "vertex.radius", &shortRadius, 1);
      anari::setParameterArray1D(
          d, indexed, "primitive.index", index.data(), 3);
      const auto gotIndexed = renderGeometry(d, indexed);
      CHECK(warnings.contains("vertex.radius"));
      checkSameImage(gotIndexed, want);
    }
  }

  SECTION("sphere UINT64 'primitive.index' is converted")
  {
    const std::vector<uint32_t> index32 = {2, 0};
    auto expected = newSpheres(d);
    anari::setParameterArray1D(
        d, expected, "primitive.index", index32.data(), 2);
    const auto want = renderGeometry(d, expected);

    const std::vector<uint64_t> index64 = {2, 0};
    auto geom = newSpheres(d);
    anari::setParameterArray1D(d, geom, "primitive.index", index64.data(), 2);
    const auto got = renderGeometry(d, geom);
    CHECK(warnings.empty());
    checkSameImage(got, want);

    SECTION("values above UINT32_MAX warn")
    {
      const std::vector<uint64_t> big = {uint64_t(UINT32_MAX) + 1};
      auto huge = newSpheres(d);
      anari::setParameterArray1D(d, huge, "primitive.index", big.data(), 1);
      const auto gotHuge = renderGeometry(d, huge);
      CHECK(warnings.contains("UINT32_MAX"));
      CHECK(hitSomething(d, gotHuge));
    }
  }

  SECTION("curve with fewer than 2 vertices has no segments")
  {
    auto none = newCurve(d, {});
    const auto gotNone = renderGeometry(d, none);
    CHECK(warnings.contains("2 vertices"));
    CHECK_FALSE(hitSomething(d, gotNone));

    warnings.clear();
    auto one = newCurve(d, {kCurvePositions[0]});
    const auto gotOne = renderGeometry(d, one);
    CHECK(warnings.contains("2 vertices"));
    CHECK_FALSE(hitSomething(d, gotOne));
  }

  SECTION("curve 'vertex.radius' shorter than the vertices uses 'radius'")
  {
    const std::vector<float> fullRadius = {0.1f, 0.1f, 0.05f, 0.05f};
    auto expected = newCurve(d);
    anari::setParameterArray1D(
        d, expected, "vertex.radius", fullRadius.data(), 4);
    const auto want = renderGeometry(d, expected);
    REQUIRE(warnings.empty());

    const std::vector<float> shortRadius = {0.1f, 0.1f};
    auto geom = newCurve(d);
    anari::setParameterArray1D(d, geom, "vertex.radius", shortRadius.data(), 2);
    const auto got = renderGeometry(d, geom);
    CHECK(warnings.contains("vertex.radius"));
    checkSameImage(got, want);
  }

  SECTION("curve 'primitive.index' past the last segment clamps to it")
  {
    const std::vector<uint32_t> expectedIndex = {0, 2, 2};
    auto expected = newCurve(d);
    anari::setParameterArray1D(
        d, expected, "primitive.index", expectedIndex.data(), 3);
    anari::setParameterArray1D(d,
        expected,
        "vertex.color",
        kCurvePositions.data(),
        kCurvePositions.size());
    const auto want = renderGeometry(d, expected);
    REQUIRE(warnings.empty());

    const std::vector<uint32_t> index = {0, 3, 1000000};
    auto geom = newCurve(d);
    anari::setParameterArray1D(d, geom, "primitive.index", index.data(), 3);
    anari::setParameterArray1D(d,
        geom,
        "vertex.color",
        kCurvePositions.data(),
        kCurvePositions.size());
    const auto got = renderGeometry(d, geom);
    CHECK(warnings.contains("primitive.index"));
    CHECK(hitSomething(d, got));
    checkSameImage(got, want);
  }

  SECTION("curve UINT64 'primitive.index' is converted")
  {
    const std::vector<uint32_t> index32 = {0, 2};
    auto expected = newCurve(d);
    anari::setParameterArray1D(
        d, expected, "primitive.index", index32.data(), 2);
    const auto want = renderGeometry(d, expected);

    const std::vector<uint64_t> index64 = {0, 2};
    auto geom = newCurve(d);
    anari::setParameterArray1D(d, geom, "primitive.index", index64.data(), 2);
    const auto got = renderGeometry(d, geom);
    CHECK(warnings.empty());
    CHECK(hitSomething(d, got));
    checkSameImage(got, want);

    SECTION("values above UINT32_MAX warn")
    {
      const std::vector<uint64_t> big = {0, uint64_t(UINT32_MAX) + 3};
      auto huge = newCurve(d);
      anari::setParameterArray1D(d, huge, "primitive.index", big.data(), 2);
      const auto gotHuge = renderGeometry(d, huge);
      CHECK(warnings.contains("UINT32_MAX"));
      CHECK(hitSomething(d, gotHuge));
    }
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
