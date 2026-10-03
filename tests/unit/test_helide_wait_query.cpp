// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Stress test for helide's object ANARI_WAIT queries: one thread renders in a
// loop while another commits scene changes and reads the world's "bounds"
// with ANARI_WAIT. The query's flush and scene update run on helide's task
// queue, so they never overlap a render (checked by running this under TSan)
// and each query sees the commit made just before it.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <atomic>
#include <thread>

namespace {

using namespace helide_test;

constexpr float kRadius = 0.5f;

// Moves 'sphere's single sphere to 'center' and commits it.
void moveSphere(anari::Device d, anari::Geometry sphere, const float3 &center)
{
  anari::setParameterArray1D(d, sphere, "vertex.position", &center, 1);
  anari::commitParameters(d, sphere);
}

} // namespace

SCENARIO("helide answers WAIT bounds queries while another thread renders",
    "[helide_wait_query]")
{
  anari::Library lib = anari::loadLibrary("helide", printErrors);
  if (lib == nullptr) {
    WARN("helide library not available; skipping helide WAIT query test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  auto sphere = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameter(d, sphere, "radius", kRadius);
  moveSphere(d, sphere, float3(0.f));
  auto material = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, material);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setParameter(d, surface, "geometry", sphere);
  anari::setAndReleaseParameter(d, surface, "material", material);
  anari::commitParameters(d, surface);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  anari::commitParameters(d, world);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 20.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::commitParameters(d, camera);
  auto renderer = anari::newObject<anari::Renderer>(d, "default");
  anari::commitParameters(d, renderer);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", uint2(16, 16));
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "world", world);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);

  GIVEN("a thread rendering the world in a loop")
  {
    std::atomic<bool> stop{false};
    std::atomic<int> frames{0};
    std::thread renderLoop([&]() {
      while (!stop) {
        anari::render(d, frame);
        anari::wait(d, frame);
        frames++;
      }
    });

    THEN("each WAIT bounds query sees the sphere moved just before it")
    {
      int wrong = 0;
      for (int i = 0; i < 200; i++) {
        const float3 center(float(i % 7), float(i % 3), 0.f);
        moveSphere(d, sphere, center);
        Box bounds{float3(0.f), float3(0.f)};
        REQUIRE(queryBounds(d, world, bounds));
        for (int a = 0; a < 3; a++) {
          if (std::abs(bounds.lower[a] - (center[a] - kRadius)) > 1e-4f
              || std::abs(bounds.upper[a] - (center[a] + kRadius)) > 1e-4f)
            wrong++;
        }
      }
      stop = true;
      renderLoop.join();

      CHECK(wrong == 0);
      CHECK(frames > 0);
    }
  }

  anari::release(d, frame);
  anari::release(d, world);
  anari::release(d, sphere);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
