// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Synchronization rules (ADR-0009): which calls must return, checked as timed
// behaviour tests. A device that breaks one usually hangs, so each Case runs
// in its own process under a timeout, and fails on any device ERROR, since
// the checks only make calls the spec allows.

#include "../AnariObject.h"
#include "../BuildContext.h"
#include "../GeometryBuilder.h"
#include "../GeometryLayout.h"
#include "../LightBuilder.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../TestDef.h"
#include "../WorldBuilder.h"
#include "Categories.h"
// std
#include <atomic>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float3;
using namespace std::chrono_literals;

// Long enough that a device that's slow but not blocked always makes it, even
// on a loaded machine: the calls these checks time either return promptly or
// wait for a render held off until the check lets it go, i.e. forever.
constexpr auto kReturnBudget = 1000ms;
// How long checkCallbackReentry() leaves the callback to run on its own
// before waiting on the frame.
constexpr auto kCallbackDeadline = 10s;
// Every Case finishes in well under a second on a conforming device.
constexpr auto kCaseTimeout = 30s;

const char *kTriangle = "ANARI_KHR_GEOMETRY_TRIANGLE";

// A world with one triangle whose vertex positions are a shared array over
// app memory, which a check maps to hold renders of the world off.
struct SharedArrayWorld
{
  std::vector<float3> positions = layoutTriangleSoup(1);
  UniqueAnariObject<anari::Array1D> positionArray;
  UniqueAnariObject<anari::World> world;

  explicit SharedArrayWorld(anari::Device d)
  {
    positionArray = UniqueAnariObject<anari::Array1D>(
        d, anari::newArray1D(d, positions.data(), positions.size()));
    auto geom = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geom, "vertex.position", positionArray.get());
    anari::commitParameters(d, geom);
    auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
    auto surface = makeSurface(d, geom, mat);
    auto light = makeDirectionalLight(d, float3(0.f, -1.f, -1.f));
    WorldContents wc;
    wc.surfaces = {surface};
    wc.lights = {light};
    world = UniqueAnariObject<anari::World>(d, assembleWorld(d, wc));
    anari::release(d, geom);
    anari::release(d, mat);
    anari::release(d, surface);
    anari::release(d, light);
  }
};

// The Tests' world, used only for the runner's camera framing: the triangle
// the checks render (the checks build their own, to map its array).
anari::World triangleWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  TriangleSpec spec;
  spec.primitiveCount = 1;
  auto geom = buildTriangleGeometry(d, spec);
  auto mat = makeMatteMaterial(d, float3(0.7f, 0.5f, 0.3f));
  auto surface = makeSurface(d, geom, mat);
  WorldContents wc;
  wc.surfaces = {surface};
  auto world = assembleWorld(d, wc);
  anari::release(d, geom);
  anari::release(d, mat);
  anari::release(d, surface);
  return world;
}

UniqueAnariObject<anari::Frame> newFrame(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  const auto f = frame.get();
  anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
  anari::setParameter(d, f, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, f, "renderer", renderer);
  anari::setParameter(d, f, "camera", camera);
  anari::setParameter(d, f, "world", world);
  return frame;
}

// Calls fn() and returns how long it took.
template <typename F>
std::chrono::duration<double, std::milli> timeOf(F &&fn)
{
  const auto start = std::chrono::steady_clock::now();
  fn();
  return std::chrono::steady_clock::now() - start;
}

// Collects a check's findings: the failures, and notes for the detail.
struct Findings
{
  std::vector<std::string> failures;
  std::vector<std::string> notes;

  void fail(std::string what)
  {
    failures.push_back(std::move(what));
  }

  // A call that has to return without waiting took 'took'.
  void returned(
      const std::string &call, std::chrono::duration<double, std::milli> took)
  {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(3);
    text << call << " took " << took.count() << " ms";
    if (took > kReturnBudget) {
      text << " (more than " << kReturnBudget.count() << " ms: it waited)";
      fail(text.str());
    } else
      notes.push_back(text.str());
  }

  BehaviorResult result(const std::string &passed) const
  {
    std::string detail;
    for (const auto &list : {failures, notes}) {
      for (const auto &item : list)
        detail += (detail.empty() ? "" : "; ") + item;
    }
    if (failures.empty())
      detail = passed + (detail.empty() ? "" : " (" + detail + ")");
    return {failures.empty(), detail};
  }
};

// With a render held off by a mapped shared array (anariMapArray: the device
// "should not execute any rendering operations" of its parents), rendering
// the frame (its first render), polling it with ANARI_NO_WAIT and querying the
// frame's and the world's properties with ANARI_NO_WAIT all return without
// waiting. After the unmap, anariFrameReady(ANARI_WAIT) returns true.
BehaviorResult checkNoWaitNeverBlocks(anari::Device d,
    anari::World,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  SharedArrayWorld scene(d);
  auto frame = newFrame(d, scene.world.get(), camera, renderer, w, h);
  const auto f = frame.get();
  anari::commitParameters(d, f);

  Findings findings;
  void *mapped = anariMapArray(d, scene.positionArray.get());
  if (mapped != scene.positions.data())
    findings.fail("mapping a shared array didn't return the app's memory");

  findings.returned(
      "anariRenderFrame", timeOf([&]() { anariRenderFrame(d, f); }));

  int ready = -1;
  findings.returned("anariFrameReady(ANARI_NO_WAIT)",
      timeOf([&]() { ready = anariFrameReady(d, f, ANARI_NO_WAIT); }));

  float duration = 0.f;
  findings.returned("frame 'duration' query with ANARI_NO_WAIT", timeOf([&]() {
    anariGetProperty(d,
        f,
        "duration",
        ANARI_FLOAT32,
        &duration,
        sizeof(duration),
        ANARI_NO_WAIT);
  }));

  anari::scenes::Bounds bounds{};
  findings.returned("world 'bounds' query with ANARI_NO_WAIT", timeOf([&]() {
    anariGetProperty(d,
        scene.world.get(),
        "bounds",
        ANARI_FLOAT32_BOX3,
        &bounds,
        sizeof(bounds),
        ANARI_NO_WAIT);
  }));

  int readyAgain = -1;
  findings.returned("anariFrameReady(ANARI_NO_WAIT), again",
      timeOf([&]() { readyAgain = anariFrameReady(d, f, ANARI_NO_WAIT); }));

  anariUnmapArray(d, scene.positionArray.get());

  if (!anariFrameReady(d, f, ANARI_WAIT))
    findings.fail(
        "anariFrameReady(ANARI_WAIT) after the unmap didn't return true");

  // A recommendation ("should not"), so a note rather than a failure: it tells
  // whether the calls above really ran with the render in flight.
  findings.notes.push_back(ready == 0 && readyAgain == 0
          ? "the render was held off while the array was mapped"
          : "the frame was ready while the array was mapped (the render wasn't "
            "held off, so nothing was in flight to wait for)");

  return findings.result("no call waited");
}

// What the completion callback in checkCallbackReentry() saw.
struct CallbackProbe
{
  anari::Array1D positionArray{nullptr};
  const void *positions{nullptr};

  std::atomic<int> calls{0};
  std::atomic<bool> finished{false};
  void *mappedArray{nullptr};
  int readyNoWait{-1};
  int durationNoWait{-1};
  int durationWait{-1};
  float duration{0.f};
  const void *mappedFrame{nullptr};
  uint32_t mappedWidth{0};
  uint32_t mappedHeight{0};
  ANARIDataType mappedType{ANARI_UNKNOWN};
};

void reentrantCallback(const void *userPtr, ANARIDevice d, ANARIFrame f)
{
  auto &p = *static_cast<CallbackProbe *>(const_cast<void *>(userPtr));
  if (p.calls++ != 0)
    return;

  // Map and unmap an array the frame's world uses.
  p.mappedArray = anariMapArray(d, p.positionArray);
  anariUnmapArray(d, p.positionArray);

  p.readyNoWait = anariFrameReady(d, f, ANARI_NO_WAIT);
  p.durationNoWait = anariGetProperty(d,
      f,
      "duration",
      ANARI_FLOAT32,
      &p.duration,
      sizeof(p.duration),
      ANARI_NO_WAIT);
  p.durationWait = anariGetProperty(d,
      f,
      "duration",
      ANARI_FLOAT32,
      &p.duration,
      sizeof(p.duration),
      ANARI_WAIT);

  p.mappedFrame = anariMapFrame(
      d, f, "channel.color", &p.mappedWidth, &p.mappedHeight, &p.mappedType);
  if (p.mappedFrame)
    anariUnmapFrame(d, f, "channel.color");

  p.finished = true;
}

// A frame's completion callback may call the API (all but
// anariFrameReady(ANARI_WAIT), per the spec's note on the callback): map and
// unmap an array the frame's world uses, poll and query its own frame, with and
// without waiting, and map the frame. It runs once, and has finished when
// anariFrameReady(ANARI_WAIT) returns.
BehaviorResult checkCallbackReentry(anari::Device d,
    anari::World,
    anari::Camera camera,
    anari::Renderer renderer,
    uint32_t w,
    uint32_t h)
{
  SharedArrayWorld scene(d);
  auto frame = newFrame(d, scene.world.get(), camera, renderer, w, h);
  const auto f = frame.get();

  CallbackProbe probe;
  probe.positionArray = scene.positionArray.get();
  probe.positions = scene.positions.data();
  anari::setParameter(d,
      f,
      "frameCompletionCallback",
      (anari::FrameCompletionCallback)reentrantCallback);
  anari::setParameter(
      d, f, "frameCompletionCallbackUserData", static_cast<void *>(&probe));
  anari::commitParameters(d, f);

  anariRenderFrame(d, f);
  // Make no call while the callback may run on another thread: calls on the
  // frame (or device) must not overlap without KHR_DEVICE_SYNCHRONIZATION. A
  // device that runs the callback inside anariFrameReady(ANARI_WAIT) instead
  // gets there after the deadline.
  const auto deadline = std::chrono::steady_clock::now() + kCallbackDeadline;
  while (!probe.finished && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  const int ready = anariFrameReady(d, f, ANARI_WAIT);
  const bool finishedByWait = probe.finished;

  Findings findings;
  if (!ready)
    findings.fail("anariFrameReady(ANARI_WAIT) didn't return true");
  if (probe.calls != 1) {
    findings.fail(
        "the callback ran " + std::to_string(probe.calls.load()) + " times");
  }
  if (!finishedByWait) {
    findings.fail(
        "the callback hadn't finished when anariFrameReady(ANARI_WAIT) "
        "returned");
  } else {
    if (probe.mappedArray != probe.positions) {
      findings.fail(
          "mapping a shared array in the callback didn't return the app's "
          "memory");
    }
    if (!probe.mappedFrame)
      findings.fail("mapping the frame's color channel in the callback failed");
    else if (probe.mappedWidth != w || probe.mappedHeight != h
        || probe.mappedType != ANARI_UFIXED8_RGBA_SRGB) {
      findings.fail(
          "the frame mapped in the callback has the wrong size or type");
    }
    // Neither is required: the spec doesn't say whether the frame counts as
    // ready while its callback runs, and 'duration' is "if available".
    findings.notes.push_back(std::string("in the callback, the frame was ")
        + (probe.readyNoWait == 1 ? "ready" : "not ready") + ", its duration "
        + (probe.durationWait == 1 || probe.durationNoWait == 1
                ? "available"
                : "unavailable"));
  }

  return findings.result("the callback's calls all returned");
}

} // namespace

void registerSynchronizationTests(Catalog &catalog)
{
  makeTest("synchronization", "no_wait_never_blocks")
      .description(
          "With a render held off by a mapped shared array, "
          "anariRenderFrame, anariFrameReady(ANARI_NO_WAIT) and ANARI_NO_WAIT "
          "property queries on the frame and world return without waiting; "
          "after the unmap, anariFrameReady(ANARI_WAIT) returns true.")
      .build(triangleWorld)
      .behavior(checkNoWaitNeverBlocks)
      .timeout(kCaseTimeout)
      .failOnDeviceErrors()
      .requireFeature(kTriangle)
      .registerInto(catalog);

  makeTest("synchronization", "completion_callback_reentry")
      .description(
          "A frame's completion callback maps and unmaps an array the "
          "frame's world uses, polls and queries its own frame (with and "
          "without waiting) and maps it; the callback runs once and has "
          "finished when anariFrameReady(ANARI_WAIT) returns.")
      .build(triangleWorld)
      .behavior(checkCallbackReentry)
      .timeout(kCaseTimeout)
      .failOnDeviceErrors()
      .requireFeatures({kTriangle, "ANARI_KHR_FRAME_COMPLETION_CALLBACK"})
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
