// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts world-build helpers
#include "cts/BuildContext.h"
#include "cts/GeometryBuilder.h"
#include "cts/GeometryLayout.h"
#include "cts/InstanceBuilder.h"
#include "cts/LightBuilder.h"
#include "cts/ParameterBinding.h"
#include "cts/SamplerBuilder.h"
#include "cts/SurfaceBuilder.h"
#include "cts/Value.h"
#include "cts/ViewBuilder.h"
#include "cts/VolumeBuilder.h"
#include "cts/WorldBuilder.h"
// std
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace anari::cts;
namespace scenes = anari::scenes;
using anari::math::float3;

namespace {

// Counts ANARI errors so a smoke test can assert a world built cleanly.
struct Status
{
  int errors = 0;
  std::string lastMessage;
};

void statusFunc(const void *userData,
    anari::Device /*device*/,
    anari::Object /*source*/,
    anari::DataType /*sourceType*/,
    anari::StatusSeverity severity,
    anari::StatusCode /*code*/,
    const char *message)
{
  if (severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_FATAL_ERROR) {
    auto *s = static_cast<Status *>(const_cast<void *>(userData));
    if (s) {
      s->errors++;
      s->lastMessage = message ? message : "";
    }
  }
}

bool finite3(const float3 &v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool validBounds(const scenes::Bounds &b)
{
  return finite3(b[0]) && finite3(b[1]) && b[0].x <= b[1].x && b[0].y <= b[1].y
      && b[0].z <= b[1].z;
}

} // namespace

// Pure: no device needed
// ////////////////////////////////////////////////////////

TEST_CASE("typed geometry specifications reject invalid layouts",
    "[cts][worldbuilder]")
{
  CHECK(parsePrimitiveMode("soup") == PrimitiveMode::Soup);
  CHECK(parsePrimitiveMode("indexed") == PrimitiveMode::Indexed);
  CHECK_THROWS_WITH(parsePrimitiveMode("strip"),
      Catch::Matchers::Contains("strip")
          && Catch::Matchers::Contains("primitive mode"));

  CHECK_THROWS_WITH(makeTriangleLayout(
                        TriangleShape::Triangle, PrimitiveMode::Soup, 0, false),
      Catch::Matchers::Contains("primitive count"));
  CHECK_THROWS_WITH(makeSphereLayout(PrimitiveMode::Soup, 4, true),
      Catch::Matchers::Contains("unused vertices")
          && Catch::Matchers::Contains("indexed"));
  CHECK_THROWS_WITH(
      makeQuadLayout(static_cast<QuadShape>(99), PrimitiveMode::Soup, 1, false),
      Catch::Matchers::Contains("quad shape"));
}

TEST_CASE("typed parameter bindings reject invalid definitions",
    "[cts][worldbuilder]")
{
  CHECK(anyToString(Any(unsetBinding())) == "none");
  CHECK(anyToString(Any(constantBinding(0.5f))) == "0.5");
  CHECK(anyToString(Any(attributeBinding("attribute0"))) == "attribute0");
  // The external label remains stable so catalog metadata and ground-truth
  // paths do not change; internally this is a typed sampler index.
  CHECK(anyToString(Any(samplerBinding(1))) == "ref_sampler_1");

  BuildContext context;
  context.set("color", Any(samplerBinding(1)));
  CHECK(
      std::get<SamplerParameter>(context.binding("color").value()).index == 1);
  context.set("color", Any(0.5f));
  CHECK_THROWS_WITH(context.binding("color"),
      Catch::Matchers::Contains("color")
          && Catch::Matchers::Contains("typed parameter binding"));

  CHECK_THROWS_WITH(
      attributeBinding(""), Catch::Matchers::Contains("attribute name"));
  CHECK_THROWS_WITH(applyParameterBinding(nullptr,
                        static_cast<anari::Object>(nullptr),
                        "color",
                        samplerBinding(2),
                        {}),
      Catch::Matchers::Contains("color")
          && Catch::Matchers::Contains("sampler index 2"));

  RawValue unsupported(ANARI_VOID_POINTER, nullptr);
  CHECK_THROWS_WITH(applyParameterValue(nullptr,
                        static_cast<anari::Object>(nullptr),
                        "callback",
                        unsupported),
      Catch::Matchers::Contains("callback")
          && Catch::Matchers::Contains("ANARI_VOID_POINTER"));
  CHECK_THROWS_WITH(newImageSampler(nullptr, "image4D", "attribute0"),
      Catch::Matchers::Contains("image4D")
          && Catch::Matchers::Contains("sampler subtype"));
}

TEST_CASE("cameraFromBounds frames a box from +Z", "[cts][worldbuilder]")
{
  scenes::Bounds b = {float3(-1.f), float3(1.f)};
  auto cam = cameraFromBounds(b);

  CHECK(cam.at.x == Approx(0.f));
  CHECK(cam.at.y == Approx(0.f));
  CHECK(cam.at.z == Approx(0.f));
  // Eye sits in front of the box on +Z, close enough that the subject fills
  // most of the frame: margin * inPlaneHalfExtent / tan(halfFovy) + halfDepth.
  // For a
  // [-1,1] box both half-extents are 1.
  const float expectedZ = 1.1f * 1.f / 0.5774f + 1.f;
  CHECK(cam.position.z == Approx(expectedZ));
  CHECK(cam.position.x == Approx(0.f));
  // Looking back toward -Z.
  CHECK(cam.direction.z == Approx(-1.f));
  CHECK(cam.up.y == Approx(1.f));
}

// Device-backed smoke tests (need helide)
// ////////////////////////////////////////

TEST_CASE("world-build helpers produce error-free worlds",
    "[cts][worldbuilder][helide]")
{
  Status status;
  anari::Library lib = anari::loadLibrary("helide", statusFunc, &status);
  if (!lib) {
    WARN("helide library not available; skipping device-backed smoke tests");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");
  REQUIRE(d != nullptr);
  anari::commitParameters(d, d);

  SECTION("each geometry subtype builds a valid, lit world")
  {
    using Factory = std::function<anari::Geometry(anari::Device)>;
    const std::vector<std::pair<std::string, Factory>> factories = {
        {"triangle",
            [](anari::Device d) {
              TriangleSpec spec;
              spec.primitiveCount = 8;
              return buildTriangleGeometry(d, spec);
            }},
        {"triangle cube",
            [](anari::Device d) {
              TriangleSpec spec;
              spec.shape = TriangleShape::Cube;
              spec.primitiveCount = 8;
              return buildTriangleGeometry(d, spec);
            }},
        {"quad",
            [](anari::Device d) {
              QuadSpec spec;
              spec.primitiveCount = 8;
              return buildQuadGeometry(d, spec);
            }},
        {"sphere",
            [](anari::Device d) {
              SphereSpec spec;
              spec.primitiveCount = 8;
              return buildSphereGeometry(d, spec);
            }},
        {"curve",
            [](anari::Device d) {
              CurveSpec spec;
              spec.primitiveCount = 8;
              return buildCurveGeometry(d, spec);
            }},
        {"cone",
            [](anari::Device d) {
              ConeSpec spec;
              spec.primitiveCount = 8;
              return buildConeGeometry(d, spec);
            }},
        {"cylinder",
            [](anari::Device d) {
              CylinderSpec spec;
              spec.primitiveCount = 8;
              return buildCylinderGeometry(d, spec);
            }},
    };

    for (const auto &[name, build] : factories) {
      INFO("geometry=" << name);
      status.errors = 0;
      auto geom = build(d);
      auto mat = makeMatteMaterial(d, float3(0.8f, 0.3f, 0.2f));
      auto surface = makeSurface(d, geom, mat);
      auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));

      WorldContents contents;
      contents.surfaces = {surface};
      contents.lights = {light};
      auto world = assembleWorld(d, contents);

      CHECK(status.errors == 0);
      INFO("last ANARI error: " << status.lastMessage);
      CHECK(status.errors == 0);
      CHECK(validBounds(worldBounds(d, world)));

      anari::release(d, geom);
      anari::release(d, mat);
      anari::release(d, surface);
      anari::release(d, light);
      anari::release(d, world);
    }
  }

  SECTION("soup and indexed variants of a geometry share their bounds")
  {
    auto buildBounds = [&](const std::string &mode) {
      TriangleSpec spec;
      spec.shape = TriangleShape::Cube;
      spec.mode = parsePrimitiveMode(mode);
      spec.primitiveCount = 6;
      auto geom = buildTriangleGeometry(d, spec);
      auto mat = makeMatteMaterial(d, float3(0.5f));
      auto surface = makeSurface(d, geom, mat);
      WorldContents contents;
      contents.surfaces = {surface};
      auto world = assembleWorld(d, contents);
      auto b = worldBounds(d, world);
      anari::release(d, geom);
      anari::release(d, mat);
      anari::release(d, surface);
      anari::release(d, world);
      return b;
    };

    status.errors = 0;
    auto soup = buildBounds("soup");
    auto indexed = buildBounds("indexed");
    CHECK(status.errors == 0);

    // Variants must produce identical geometry (the premise of a Variant axis).
    for (int i = 0; i < 2; ++i) {
      CHECK(soup[i].x == Approx(indexed[i].x));
      CHECK(soup[i].y == Approx(indexed[i].y));
      CHECK(soup[i].z == Approx(indexed[i].z));
    }
  }

  SECTION("a structuredRegular volume world builds cleanly")
  {
    status.errors = 0;
    auto field = makeStructuredRegularField(d, {8, 8, 8});
    auto volume = makeVolume(d, field);
    WorldContents contents;
    contents.volumes = {volume};
    auto world = assembleWorld(d, contents);

    CHECK(status.errors == 0);
    INFO("last ANARI error: " << status.lastMessage);
    CHECK(validBounds(worldBounds(d, world)));

    anari::release(d, field);
    anari::release(d, volume);
    anari::release(d, world);
  }

  SECTION("an instanced surface world builds cleanly")
  {
    status.errors = 0;
    SphereSpec spec;
    spec.primitiveCount = 4;
    auto geom = buildSphereGeometry(d, spec);
    auto mat = makeMatteMaterial(d, float3(0.2f, 0.6f, 0.9f));
    auto surface = makeSurface(d, geom, mat);
    auto instance = makeInstance(d, {surface});

    WorldContents contents;
    contents.instances = {instance};
    auto world = assembleWorld(d, contents);

    CHECK(status.errors == 0);
    INFO("last ANARI error: " << status.lastMessage);
    CHECK(validBounds(worldBounds(d, world)));

    anari::release(d, geom);
    anari::release(d, mat);
    anari::release(d, surface);
    anari::release(d, instance);
    anari::release(d, world);
  }

  SECTION("a perspective camera accepts optional intrinsics")
  {
    status.errors = 0;
    scenes::Bounds b = {float3(-1.f), float3(1.f)};
    auto camDesc = cameraFromBounds(b);
    PerspectiveCameraOptions opts;
    opts.fovy = 0.6f;
    opts.aspect = 1.77f;
    opts.near = 0.01f;
    opts.far = 100.f;
    auto camera = makePerspectiveCamera(d, camDesc, opts);
    CHECK(status.errors == 0); // fovy/aspect supported; near/far at most warn
    INFO("last ANARI error: " << status.lastMessage);
    anari::release(d, camera);
  }

  SECTION("a built world renders a color frame")
  {
    status.errors = 0;
    TriangleSpec spec;
    spec.primitiveCount = 8;
    auto geom = buildTriangleGeometry(d, spec);
    auto mat = makeMatteMaterial(d, float3(0.8f));
    auto surface = makeSurface(d, geom, mat);
    auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
    WorldContents contents;
    contents.surfaces = {surface};
    contents.lights = {light};
    auto world = assembleWorld(d, contents);

    const uint32_t w = 64, h = 64;
    auto frame = makeColorFrame(d, world, w, h);
    anari::render(d, frame);
    anari::wait(d, frame);

    auto fb = anari::map<uint32_t>(d, frame, "channel.color");
    CHECK(fb.data != nullptr);
    CHECK(fb.width == w);
    CHECK(fb.height == h);
    anari::unmap(d, frame, "channel.color");

    CHECK(status.errors == 0);
    INFO("last ANARI error: " << status.lastMessage);

    anari::release(d, geom);
    anari::release(d, mat);
    anari::release(d, surface);
    anari::release(d, light);
    anari::release(d, world);
    anari::release(d, frame);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}
