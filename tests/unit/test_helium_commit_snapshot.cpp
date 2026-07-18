// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// This is an end-to-end regression for helium's commit-snapshot behavior,
// exercised through helide: anariCommitParameters() must capture the object's
// parameters at the commit call, so a setParameter() issued before the commit
// buffer flushes cannot leak into that commit. helium has no standalone device,
// so the behavior is observed through helide's framebuffer: helide's renderer
// reads "background" in commitParameters() and, with an empty world, fills the
// whole frame with it -- making the committed value directly observable.

#include "catch.hpp"

#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>

namespace {

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ANARI][ERROR][%p] %s\n", source, message);
  }
}

// Read the top-left pixel of the frame's FLOAT32_VEC4 color channel.
static anari::math::float4 firstPixel(anari::Device d, anari::Frame frame)
{
  auto mapped = anari::map<anari::math::float4>(d, frame, "channel.color");
  anari::math::float4 px =
      mapped.data ? mapped.data[0] : anari::math::float4(-1.f);
  anari::unmap(d, frame, "channel.color");
  return px;
}

SCENARIO(
    "commit snapshot: a late setParameter does not leak into a prior "
    "commit",
    "[helide][helium_commit_snapshot]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping commit-snapshot test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  GIVEN("A frame with an empty world (background fills the image)")
  {
    auto world = anari::newObject<anari::World>(d);
    anari::commitParameters(d, world);

    auto camera = anari::newObject<anari::Camera>(d, "perspective");
    anari::setParameter(d, camera, "position", anari::math::float3(0, 0, 0));
    anari::setParameter(d, camera, "direction", anari::math::float3(0, 0, 1));
    anari::setParameter(d, camera, "up", anari::math::float3(0, 1, 0));
    anari::setParameter(d, camera, "aspect", 1.f);
    anari::commitParameters(d, camera);

    auto renderer = anari::newObject<anari::Renderer>(d, "default");

    auto frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", anari::math::uint2(4, 4));
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    anari::setParameter(d, frame, "world", world);
    anari::setParameter(d, frame, "camera", camera);
    anari::setParameter(d, frame, "renderer", renderer);
    anari::commitParameters(d, frame);

    const anari::math::float4 A(1.f, 0.f, 0.f, 1.f); // red
    const anari::math::float4 B(0.f, 1.f, 0.f, 1.f); // green

    WHEN("background is committed as A, then set to B without committing")
    {
      anari::setParameter(d, renderer, "background", A);
      anari::commitParameters(d, renderer); // snapshot captures A

      anari::setParameter(d, renderer, "background", B); // late set, no commit

      anari::render(d, frame);
      anari::wait(d, frame);

      THEN("the render reflects the committed value A, not the late value B")
      {
        auto px = firstPixel(d, frame);
        REQUIRE(px.x == Approx(A.x));
        REQUIRE(px.y == Approx(A.y));
      }

      AND_WHEN("the renderer is committed again")
      {
        anari::commitParameters(d, renderer); // snapshot now captures B

        anari::render(d, frame);
        anari::wait(d, frame);

        THEN("the subsequent commit applies B")
        {
          auto px = firstPixel(d, frame);
          REQUIRE(px.x == Approx(B.x));
          REQUIRE(px.y == Approx(B.y));
        }
      }
    }

    anari::release(d, world);
    anari::release(d, camera);
    anari::release(d, renderer);
    anari::release(d, frame);
  }

  anari::release(d, d);
  anari::unloadLibrary(lib);
}

// Exercises the commit-vs-flush race the snapshot mutex closes: a frame is put
// in flight (rendered asynchronously, so the device's worker flushes and reads
// each object's committed snapshot) while the app keeps committing those same
// objects on the calling thread (re-writing their committed snapshots). This is
// legal without any multi-threading on the app's part -- the device's render
// worker is the second thread. Without serialization the flush read races the
// snapshot write; the test just has to run to completion without crashing or
// deadlocking. Best run under ThreadSanitizer, but valuable unsanitized too.
SCENARIO(
    "commit snapshot: committing objects while a frame is in flight is "
    "race-free",
    "[helide][helium_commit_snapshot]")
{
  anari::Library lib = anari::loadLibrary("helide", statusFunc, nullptr);
  if (lib == nullptr) {
    WARN("helide library not available; skipping commit-snapshot race test");
    return;
  }

  anari::Device d = anari::newDevice(lib, "default");

  auto sphere = anari::newObject<anari::Geometry>(d, "sphere");
  auto positions = anari::newArray1D(d, ANARI_FLOAT32_VEC3, 1);
  {
    auto *p = anari::map<anari::math::float3>(d, positions);
    p[0] = anari::math::float3(0.f);
    anari::unmap(d, positions);
  }
  anari::setParameter(d, sphere, "vertex.position", positions);
  anari::release(d, positions);

  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, material);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", sphere);
  anari::setParameter(d, surface, "material", material);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  {
    auto surfaces = anari::newArray1D(d, ANARI_SURFACE, 1);
    auto *s = anari::map<anari::Surface>(d, surfaces);
    s[0] = surface;
    anari::unmap(d, surfaces);
    anari::setParameter(d, world, "surface", surfaces);
    anari::release(d, surfaces);
  }
  anari::commitParameters(d, world);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", anari::math::float3(0, 0, -3));
  anari::setParameter(d, camera, "direction", anari::math::float3(0, 0, 1));
  anari::setParameter(d, camera, "up", anari::math::float3(0, 1, 0));
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", anari::math::uint2(32, 32));
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, frame, "world", world);
  anari::setParameter(d, frame, "camera", camera);
  anari::setParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);

  WHEN("objects are re-committed while frames render asynchronously")
  {
    const int kIterations = 300;
    for (int i = 0; i < kIterations; ++i) {
      // Kick a render: the device worker flushes (reading committed snapshots)
      // and renders in the background.
      anari::render(d, frame);

      // ...and immediately re-commit the same objects on this thread, racing
      // the in-flight flush's reads of their committed snapshots.
      anari::setParameter(
          d, sphere, "radius", 0.1f + 0.001f * static_cast<float>(i));
      anari::commitParameters(d, sphere);
      anari::setParameter(
          d, renderer, "ambientRadiance", static_cast<float>(i));
      anari::commitParameters(d, renderer);
      anari::commitParameters(d, world);

      anari::wait(d, frame);
    }

    THEN("the loop completes without crashing or deadlocking")
    {
      REQUIRE(true);
    }
  }

  anari::release(d, sphere);
  anari::release(d, material);
  anari::release(d, surface);
  anari::release(d, world);
  anari::release(d, camera);
  anari::release(d, renderer);
  anari::release(d, frame);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

} // namespace
