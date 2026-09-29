// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Regressions for waits helide can't satisfy. A queued render waits for every
// mapped array to be unmapped, so a thread holding a mapped array can't wait
// for that render, or for device work queued behind it. helide reports an
// ERROR and returns instead of hanging (anariGetProperty() with ANARI_WAIT,
// anariFrameReady() with ANARI_WAIT, anariMapFrame(), anariRenderFrame()),
// and a release that privatizes a shared array does so without waiting, with
// a WARNING. Also, an ANARI_WAIT query from a status callback raised while
// helide's worker flushes doesn't flush again.

#include "catch.hpp"
#include "helide_render_test.h"

// std
#include <algorithm>
#include <cstring>
#include <memory>

namespace {

using namespace helide_test;

// An array the calling thread holds mapped until unmap() (or destruction).
struct MappedArray
{
  MappedArray(anari::Device d)
      : m_device(d),
        m_array(anariNewArray1D(d, nullptr, nullptr, nullptr, ANARI_FLOAT32, 4))
  {
    anariMapArray(m_device, m_array);
  }

  ~MappedArray()
  {
    unmap();
    anari::release(m_device, m_array);
  }

  void unmap()
  {
    if (m_mapped)
      anariUnmapArray(m_device, m_array);
    m_mapped = false;
  }

 private:
  anari::Device m_device{nullptr};
  ANARIArray1D m_array{nullptr};
  bool m_mapped{true};
};

// Whether 'bounds' is the box around kCenterTriangle.
bool boundsTriangle(const Box &bounds)
{
  const float3 lower(-0.5f, -0.5f, 0.f);
  const float3 upper(0.5f, 0.5f, 0.f);
  return linalg::maxelem(linalg::abs(bounds.lower - lower)) < 1e-4f
      && linalg::maxelem(linalg::abs(bounds.upper - upper)) < 1e-4f;
}

// Maps 'frame's color channel; nullptr if helide refused.
const void *mapColor(anari::Device d, anari::Frame frame)
{
  uint32_t width = 0;
  uint32_t height = 0;
  ANARIDataType type = ANARI_UNKNOWN;
  return anariMapFrame(d, frame, "channel.color", &width, &height, &type);
}

} // namespace

SCENARIO("helide refuses to wait on a render the calling thread's map blocks",
    "[helide_wait_deadlock]")
{
  StatusLog log;
  anari::Library lib = anari::loadLibrary("helide", collectStatus, &log);
  if (lib == nullptr) {
    WARN("helide library not available; skipping wait deadlock test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  auto vertices = std::make_unique<float3[]>(3);
  std::copy(kCenterTriangle, kCenterTriangle + 3, vertices.get());
  auto triangle = makeTriangleWorld(d, vertices.get());
  auto world = triangle.world;
  auto frame = newFrame(d, world);

  GIVEN("a thread that holds a mapped array while no render is queued")
  {
    MappedArray mapped(d);

    THEN("its ANARI_WAIT property query waits as usual")
    {
      Box bounds{};
      CHECK(queryBounds(d, world, bounds));
      CHECK(boundsTriangle(bounds));
      CHECK(log.errors.empty());
    }
  }

  GIVEN("a thread that holds a mapped array while a render is queued")
  {
    MappedArray mapped(d);
    anari::render(d, frame);

    THEN("its ANARI_WAIT property query returns 0 with an ERROR")
    {
      Box bounds{};
      CHECK(!queryBounds(d, world, bounds));
      CHECK(
          log.errors.contains("anariGetProperty() with ANARI_WAIT would "
                              "deadlock"));

      AND_THEN("after unmapping, the query succeeds")
      {
        mapped.unmap();
        log.clear();
        CHECK(queryBounds(d, world, bounds));
        CHECK(boundsTriangle(bounds));
        CHECK(log.errors.empty());
      }
    }

    THEN("its anariFrameReady() with ANARI_WAIT returns 0 with an ERROR")
    {
      CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 0);
      CHECK(
          log.errors.contains("anariFrameReady() with ANARI_WAIT would "
                              "deadlock"));

      AND_THEN("after unmapping, the wait succeeds")
      {
        mapped.unmap();
        log.clear();
        CHECK(anariFrameReady(d, frame, ANARI_WAIT) == 1);
        CHECK(log.errors.empty());
      }
    }

    THEN("its anariMapFrame() returns nullptr with an ERROR")
    {
      CHECK(mapColor(d, frame) == nullptr);
      CHECK(log.errors.contains("anariMapFrame() would deadlock"));

      AND_THEN("after unmapping, the frame maps")
      {
        mapped.unmap();
        log.clear();
        CHECK(mapColor(d, frame) != nullptr);
        anariUnmapFrame(d, frame, "channel.color");
        CHECK(log.errors.empty());
      }
    }

    THEN("rendering the frame again reports an ERROR instead of waiting")
    {
      anari::render(d, frame);
      CHECK(log.errors.contains("anariRenderFrame() would deadlock"));

      AND_THEN("after unmapping, the frame renders again")
      {
        mapped.unmap();
        log.clear();
        anari::wait(d, frame);
        anari::render(d, frame);
        anari::wait(d, frame);
        CHECK(log.errors.empty());
      }
    }

    mapped.unmap();
    anari::wait(d, frame);
  }

  anari::release(d, frame);
  anari::release(d, triangle.positions);
  anari::release(d, triangle.surface);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

SCENARIO(
    "helide privatizes a released shared array without waiting on a "
    "render the calling thread's map blocks",
    "[helide_wait_deadlock]")
{
  StatusLog log;
  anari::Library lib = anari::loadLibrary("helide", collectStatus, &log);
  if (lib == nullptr) {
    WARN("helide library not available; skipping wait deadlock test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  auto vertices = std::make_unique<float3[]>(3);
  std::copy(kCenterTriangle, kCenterTriangle + 3, vertices.get());
  auto [world, surface, positions, geometry] =
      makeTriangleWorld(d, vertices.get());
  const auto expected = renderChannels(d, world);
  REQUIRE(countHits(expected.depth) > 0);
  auto frame = newFrame(d, world);

  GIVEN("a thread that holds a mapped array while a render is queued")
  {
    MappedArray mapped(d);
    anari::render(d, frame);

    WHEN("it releases the last public reference to the vertex array")
    {
      anari::release(d, positions);

      THEN("the release returns with a WARNING and no ERROR")
      {
        CHECK(log.warnings.contains("may still read the app's memory"));
        CHECK(log.errors.empty());

        AND_THEN("later renders read the array's copy of the vertices")
        {
          // The queued render may still read the app's buffer: keep it alive
          // until that render ends.
          moveOutOfView(vertices.get());
          mapped.unmap();
          anari::wait(d, frame);
          vertices.reset();

          forceSceneRebuild(d, surface);
          anari::render(d, frame);
          checkSameImage(readChannels(d, frame), expected);
        }
      }
    }

    mapped.unmap();
    anari::wait(d, frame);
  }

  anari::release(d, frame);
  anari::release(d, surface);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

namespace {

// State for queryOnFrameWarning(): the triangle world it queries, and what it
// saw.
struct NestedQuery
{
  StatusLog log;
  TriangleWorld triangle;
  bool queried{false};
  bool found{false};
  Box bounds{};
};

// Status callback that records errors and warnings (see collectStatus()) and,
// on the first warning about a frame missing its renderer (raised while
// helide's worker flushes the frame's commit), moves the triangle out of view,
// commits it, and queries the world's bounds with ANARI_WAIT.
void queryOnFrameWarning(const void *userPtr,
    ANARIDevice d,
    ANARIObject source,
    ANARIDataType type,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  auto *nested = (NestedQuery *)userPtr;
  collectStatus(&nested->log, d, source, type, severity, code, message);
  if (severity != ANARI_SEVERITY_WARNING || nested->queried
      || !std::strstr(message, "missing required parameter 'renderer'"))
    return;
  nested->queried = true;

  float3 moved[3];
  moveOutOfView(moved);
  anari::setParameterArray1D(
      d, nested->triangle.geometry, "vertex.position", moved, 3);
  anari::commitParameters(d, nested->triangle.geometry);
  nested->found = queryBounds(d, nested->triangle.world, nested->bounds);
}

// Status callback that records errors and warnings (see collectStatus()) and,
// on the first warning that a release privatizes an array without waiting,
// queries the world's bounds with ANARI_WAIT.
void queryOnReleaseWarning(const void *userPtr,
    ANARIDevice d,
    ANARIObject source,
    ANARIDataType type,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  auto *nested = (NestedQuery *)userPtr;
  collectStatus(&nested->log, d, source, type, severity, code, message);
  if (severity != ANARI_SEVERITY_WARNING || nested->queried
      || !std::strstr(message, "may still read the app's memory"))
    return;
  nested->queried = true;
  nested->found = queryBounds(d, nested->triangle.world, nested->bounds);
}

} // namespace

SCENARIO(
    "a status callback's ANARI_WAIT query during helide's flush doesn't "
    "flush again",
    "[helide_wait_deadlock]")
{
  NestedQuery nested;
  anari::Library lib =
      anari::loadLibrary("helide", queryOnFrameWarning, &nested);
  if (lib == nullptr) {
    WARN("helide library not available; skipping wait deadlock test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  float3 vertices[3];
  std::copy(kCenterTriangle, kCenterTriangle + 3, vertices);
  nested.triangle = makeTriangleWorld(d, vertices);
  auto world = nested.triangle.world;

  GIVEN(
      "a frame committed without a renderer, which helide warns about as "
      "it flushes the commit")
  {
    auto frame = newFrame(d, world);
    anari::unsetParameter(d, frame, "renderer");
    anari::commitParameters(d, frame);

    WHEN(
        "the app's ANARI_WAIT query flushes it, and the warning's callback "
        "commits a change and queries with ANARI_WAIT")
    {
      Box bounds{};
      REQUIRE(queryBounds(d, world, bounds));
      REQUIRE(nested.queried);

      THEN("the callback's query answers without flushing the change")
      {
        CHECK(nested.found);
        CHECK(boundsTriangle(nested.bounds));
        CHECK(boundsTriangle(bounds));
        CHECK(nested.log.errors.empty());

        AND_THEN("the next query flushes it")
        {
          REQUIRE(queryBounds(d, world, bounds));
          CHECK(bounds.lower.x > 50.f);
        }
      }
    }

    anari::release(d, frame);
  }

  anari::release(d, nested.triangle.positions);
  anari::release(d, nested.triangle.surface);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}

SCENARIO(
    "a status callback's ANARI_WAIT query during a release that can't "
    "wait is refused",
    "[helide_wait_deadlock]")
{
  NestedQuery nested;
  anari::Library lib =
      anari::loadLibrary("helide", queryOnReleaseWarning, &nested);
  if (lib == nullptr) {
    WARN("helide library not available; skipping wait deadlock test");
    return;
  }
  anari::Device d = anari::newDevice(lib, "default");

  float3 vertices[3];
  std::copy(kCenterTriangle, kCenterTriangle + 3, vertices);
  nested.triangle = makeTriangleWorld(d, vertices);
  auto world = nested.triangle.world;
  auto frame = newFrame(d, world);

  GIVEN("a thread that holds a mapped array while a render is queued")
  {
    MappedArray mapped(d);
    anari::render(d, frame);

    WHEN(
        "it releases the vertex array, and the release's warning callback "
        "queries with ANARI_WAIT")
    {
      anari::release(d, nested.triangle.positions);
      REQUIRE(nested.queried);

      THEN("the query is refused as the thread's own would be")
      {
        CHECK(!nested.found);
        CHECK(nested.log.errors.contains(
            "anariGetProperty() with ANARI_WAIT would deadlock"));
      }
    }

    mapped.unmap();
    anari::wait(d, frame);
  }

  anari::release(d, frame);
  anari::release(d, nested.triangle.surface);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(lib);
}
