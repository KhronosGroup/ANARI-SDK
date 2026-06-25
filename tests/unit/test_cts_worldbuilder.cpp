// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
// cts world-build helpers
#include "cts/WorldBuilder.h"
// std
#include <cmath>
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

// Pure: no device needed ////////////////////////////////////////////////////////

TEST_CASE("cameraFromBounds frames a box from +Z", "[cts][worldbuilder]")
{
  scenes::Bounds b = {float3(-1.f), float3(1.f)};
  auto cam = cameraFromBounds(b);

  CHECK(cam.at.x == Approx(0.f));
  CHECK(cam.at.y == Approx(0.f));
  CHECK(cam.at.z == Approx(0.f));
  // Eye is pushed out along +Z by the bounds diagonal length.
  CHECK(cam.position.z == Approx(std::sqrt(12.f)));
  CHECK(cam.position.x == Approx(0.f));
  // Looking back toward -Z.
  CHECK(cam.direction.z == Approx(-1.f));
  CHECK(cam.up.y == Approx(1.f));
}

// Device-backed smoke tests (need helide) ////////////////////////////////////////

TEST_CASE("world-build helpers produce error-free worlds", "[cts][worldbuilder][helide]")
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
    struct Spec
    {
      std::string subtype;
      std::string shape;
    };
    const std::vector<Spec> specs = {
        {"triangle", "triangle"},
        {"triangle", "cube"},
        {"quad", "quad"},
        {"sphere", ""},
        {"curve", ""},
        {"cone", ""},
        {"cylinder", ""},
    };

    for (const auto &spec : specs) {
      INFO("subtype=" << spec.subtype << " shape=" << spec.shape);
      status.errors = 0;

      GeometryOptions opts;
      opts.subtype = spec.subtype;
      if (!spec.shape.empty())
        opts.shape = spec.shape;
      opts.primitiveCount = 8;
      opts.seed = 42;

      auto geom = buildGeometry(d, opts);
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
      GeometryOptions opts;
      opts.subtype = "triangle";
      opts.shape = "cube";
      opts.primitiveMode = mode;
      opts.primitiveCount = 6;
      opts.seed = 7;
      auto geom = buildGeometry(d, opts);
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
    auto field = makeStructuredRegularField(d, {8, 8, 8}, 1);
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
    GeometryOptions opts;
    opts.subtype = "sphere";
    opts.primitiveCount = 4;
    auto geom = buildGeometry(d, opts);
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
    GeometryOptions opts;
    opts.subtype = "triangle";
    opts.primitiveCount = 8;
    auto geom = buildGeometry(d, opts);
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
