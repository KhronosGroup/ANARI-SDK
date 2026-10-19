// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// KHR_DEVICE_SYNCHRONIZATION: frames wait for mapped arrays to be unmapped,
// arrays mapped while frames render are never read half-written, completion
// callbacks may map arrays, waiting on another frame from a completion
// callback reports an error instead of deadlocking, anariGetProperty() with
// ANARI_WAIT runs behind in-flight renders, waiting on the device from a
// thread that has an array mapped reports an error instead of deadlocking,
// renders dropped for either reason still fire the completion callback,
// device work nested in a render joins it, racing maps and unmaps of one array
// stay consistent, a mapped array's release unmaps it, and a frame released
// without being waited on isn't leaked. Hangs show up as a ctest timeout.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace anari::math;
using namespace std::chrono_literals;

constexpr uint2 IMAGE_SIZE = {16, 16};
const float4 RED = {1.f, 0.f, 0.f, 1.f};
const float4 BLUE = {0.f, 0.f, 1.f, 1.f};
const float4 GREEN = {0.f, 1.f, 0.f, 1.f};

// Status messages at WARNING severity or worse
struct Messages
{
  std::mutex mutex;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex);
    warnings.clear();
    errors.clear();
  }

  void add(ANARIStatusSeverity severity, const char *message)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (severity <= ANARI_SEVERITY_ERROR)
      errors.push_back(message);
    else
      warnings.push_back(message);
  }

  bool has(const char *text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto *list : {&warnings, &errors}) {
      for (auto &m : *list) {
        if (m.find(text) != std::string::npos)
          return true;
      }
    }
    return false;
  }

  size_t numErrors()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return errors.size();
  }

  // True if an error (not a warning) contains 'text'
  bool hasError(const char *text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &m : errors) {
      if (m.find(text) != std::string::npos)
        return true;
    }
    return false;
  }
} g_messages;

// Called with every status message at WARNING severity or worse, on the
// thread that reported it
std::function<void(const char *)> g_statusHook;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity > ANARI_SEVERITY_WARNING)
    return;
  std::fprintf(stderr, "[ANARI] %s\n", message);
  g_messages.add(severity, message);
  if (g_statusHook)
    g_statusHook(message);
}

bool report(const char *test, bool pass)
{
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// A quad covering an orthographic camera's whole [-1, 1]^2 view, colored by
// its (managed, mappable) vertex colors; eye-lit shading of a quad facing the
// camera gives the interpolated vertex color exactly
struct Scene
{
  anari::Device device{nullptr};
  anari::Array1D positions{nullptr}; // float3 x 4
  anari::Array1D colors{nullptr}; // float4 x 4
  anari::World world{nullptr};
  anari::Camera camera{nullptr};
  anari::Renderer renderer{nullptr};

  void setPositions(float halfSize)
  {
    auto *p = anari::map<float3>(device, positions);
    p[0] = float3(-halfSize, -halfSize, 0.f);
    p[1] = float3(halfSize, -halfSize, 0.f);
    p[2] = float3(halfSize, halfSize, 0.f);
    p[3] = float3(-halfSize, halfSize, 0.f);
    anari::unmap(device, positions);
  }

  void setColors(float4 c)
  {
    auto *p = anari::map<float4>(device, colors);
    for (int i = 0; i < 4; i++)
      p[i] = c;
    anari::unmap(device, colors);
  }
};

Scene makeScene(anari::Device d)
{
  Scene s;
  s.device = d;
  s.positions = anari::newArray1D(d, ANARI_FLOAT32_VEC3, 4);
  s.colors = anari::newArray1D(d, ANARI_FLOAT32_VEC4, 4);
  s.setPositions(2.f);
  s.setColors(RED);

  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameter(d, geom, "vertex.position", s.positions);
  anari::setParameter(d, geom, "vertex.color", s.colors);
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", "color");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  s.world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, s.world, "surface", &surface, 1);
  anari::commitParameters(d, s.world);
  anari::release(d, surface);

  s.camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, s.camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, s.camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, s.camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, s.camera, "height", 2.f);
  anari::commitParameters(d, s.camera);

  s.renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::commitParameters(d, s.renderer);
  return s;
}

void releaseScene(Scene &s)
{
  anari::release(s.device, s.renderer);
  anari::release(s.device, s.camera);
  anari::release(s.device, s.world);
  anari::release(s.device, s.colors);
  anari::release(s.device, s.positions);
}

anari::Frame makeFrame(const Scene &s)
{
  auto d = s.device;
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "world", s.world);
  anari::setParameter(d, frame, "camera", s.camera);
  anari::setParameter(d, frame, "renderer", s.renderer);
  anari::commitParameters(d, frame);
  return frame;
}

bool near(float4 a, float4 b)
{
  return std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.y - b.y) < 1e-4f
      && std::fabs(a.z - b.z) < 1e-4f && std::fabs(a.w - b.w) < 1e-4f;
}

// The frame's color if every pixel has the same one, NaN otherwise
float4 uniformColor(anari::Device d, anari::Frame frame)
{
  auto fb = anari::map<float4>(d, frame, "channel.color");
  float4 c(NAN);
  if (fb.data && fb.width * fb.height != 0) {
    c = fb.data[0];
    for (size_t i = 1; i < size_t(fb.width) * fb.height; i++) {
      if (!near(fb.data[i], c)) {
        c = float4(NAN);
        break;
      }
    }
  }
  anari::unmap(d, frame, "channel.color");
  return c;
}

// Polls the frame for up to 'timeout'
bool readyWithin(
    anari::Device d, anari::Frame f, std::chrono::milliseconds timeout)
{
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < end) {
    if (anari::isReady(d, f))
      return true;
    std::this_thread::sleep_for(1ms);
  }
  return anari::isReady(d, f);
}

bool testExtensionDeclared(anari::Library library)
{
  bool found = false;
  auto *extensions = anariGetDeviceExtensions(library, "default");
  for (auto e = extensions; e && *e; ++e)
    found |= std::strcmp(*e, "ANARI_KHR_DEVICE_SYNCHRONIZATION") == 0;
  return report("extension declared", found);
}

// A render issued while an array is mapped doesn't start until it's unmapped,
// then sees what was written while mapped
bool testFrameWaitsForMappedArray(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  auto *c = anari::map<float4>(d, s.colors);
  anari::render(d, frame);
  std::this_thread::sleep_for(100ms);
  if (anari::isReady(d, frame)) {
    std::printf("FAIL frame waits for mapped array: rendered while mapped\n");
    pass = false;
  }
  for (int i = 0; i < 4; i++)
    c[i] = GREEN;
  anari::unmap(d, s.colors);

  if (!readyWithin(d, frame, 5000ms)) {
    std::printf("FAIL frame waits for mapped array: not rendered on unmap\n");
    pass = false;
  }
  anari::wait(d, frame);
  if (!near(uniformColor(d, frame), GREEN)) {
    std::printf("FAIL frame waits for mapped array: wrong color\n");
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL frame waits for mapped array: unexpected errors\n");
    pass = false;
  }
  anari::release(d, frame);
  return report("frame waits for mapped array", pass);
}

// Mapping an array twice and unmapping it once (helium warns) unmaps it;
// unmapping it again (helium warns) doesn't let a later mapping go unnoticed
bool testUnbalancedMapping(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  anari::map<float4>(d, s.colors);
  anari::map<float4>(d, s.colors);
  anari::unmap(d, s.colors);
  anari::render(d, frame);
  if (!readyWithin(d, frame, 5000ms)) {
    std::printf("FAIL unbalanced mapping: double map blocks rendering\n");
    pass = false;
  }
  anari::wait(d, frame);

  anari::unmap(d, s.colors);
  anari::map<float4>(d, s.colors);
  anari::render(d, frame);
  std::this_thread::sleep_for(50ms);
  if (anari::isReady(d, frame)) {
    std::printf("FAIL unbalanced mapping: rendered while mapped\n");
    pass = false;
  }
  anari::unmap(d, s.colors);
  anari::wait(d, frame);

  if (!g_messages.has("array mapped again")
      || !g_messages.has("array unmapped again")) {
    std::printf("FAIL unbalanced mapping: missing helium warnings\n");
    pass = false;
  }
  anari::release(d, frame);
  return report("unbalanced mapping", pass);
}

struct MappingCallbackState
{
  anari::Device device{nullptr};
  anari::Array1D colors{nullptr};
  int calls{0};
};

void mappingCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  auto *cb = (MappingCallbackState *)userData;
  cb->calls++;
  auto *c = (float4 *)anariMapArray(cb->device, cb->colors);
  for (int i = 0; i < 4; i++)
    c[i] = BLUE;
  anariUnmapArray(cb->device, cb->colors);
}

// A completion callback may map and modify an array; the next render sees it
bool testCallbackMapsArray(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);
  s.setColors(RED);

  MappingCallbackState cb;
  cb.device = d;
  cb.colors = s.colors;
  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)mappingCallback);
  anari::setParameter(d, frame, "frameCompletionCallbackUserData", (void *)&cb);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  const float4 first = uniformColor(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);
  const float4 second = uniformColor(d, frame);

  if (cb.calls != 2) {
    std::printf("FAIL callback maps array: callback ran %d times\n", cb.calls);
    pass = false;
  }
  if (!near(first, RED) || !near(second, BLUE)) {
    std::printf("FAIL callback maps array: renders didn't see the colors\n");
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL callback maps array: unexpected errors\n");
    pass = false;
  }
  anari::release(d, frame);
  return report("callback maps array", pass);
}

struct CrossFrameCallbackState
{
  anari::Device device{nullptr};
  anari::Frame other{nullptr};
  int readyWait{-1};
  int readyNoWait{-1};
  int gotDuration{-1};
};

void crossFrameCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  auto *cb = (CrossFrameCallbackState *)userData;
  // the other frame's render is queued behind the one running this callback
  cb->readyNoWait = anariFrameReady(cb->device, cb->other, ANARI_NO_WAIT);
  cb->readyWait = anariFrameReady(cb->device, cb->other, ANARI_WAIT);
  float duration = 0.f;
  cb->gotDuration = anariGetProperty(cb->device,
      cb->other,
      "duration",
      ANARI_FLOAT32,
      &duration,
      sizeof(duration),
      ANARI_WAIT);
}

// Waiting from a completion callback on a frame queued behind it reports an
// error and returns not-ready instead of deadlocking
bool testCrossFrameWait(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto a = makeFrame(s);
  auto b = makeFrame(s);

  CrossFrameCallbackState cb;
  cb.device = d;
  cb.other = b;
  anari::setParameter(d,
      a,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)crossFrameCallback);
  anari::setParameter(d, a, "frameCompletionCallbackUserData", (void *)&cb);
  anari::commitParameters(d, a);

  // a mapped array holds off both renders until both are queued
  anari::map<float4>(d, s.colors);
  anari::render(d, a);
  anari::render(d, b);
  anari::unmap(d, s.colors);
  anari::wait(d, a);
  anari::wait(d, b);

  if (cb.readyNoWait != 0 || cb.readyWait != 0) {
    std::printf("FAIL cross frame wait: frameReady() returned %d, %d\n",
        cb.readyNoWait,
        cb.readyWait);
    pass = false;
  }
  if (cb.gotDuration != 1) {
    std::printf("FAIL cross frame wait: getProperty(WAIT) failed\n");
    pass = false;
  }
  if (!g_messages.has("deadlock")) {
    std::printf("FAIL cross frame wait: no error reported\n");
    pass = false;
  }
  if (!near(uniformColor(d, b), uniformColor(d, a))) {
    std::printf("FAIL cross frame wait: second frame not rendered\n");
    pass = false;
  }
  anari::release(d, a);
  anari::release(d, b);
  return report("cross frame wait", pass);
}

// anariGetProperty(ANARI_WAIT) runs behind renders already issued, so a
// frame's duration is that of its latest render
bool testWaitPropertyAfterRender(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  anari::render(d, frame);
  float duration = 0.f;
  anari::getProperty(d, frame, "duration", duration, ANARI_WAIT);
  if (!(duration > 0.f)) {
    std::printf(
        "FAIL wait property: duration %f before render finished\n", duration);
    pass = false;
  }
  anari::wait(d, frame);
  anari::release(d, frame);
  return report("getProperty(WAIT) after render", pass);
}

// A thread that has an array mapped and waits on the device (getProperty
// with ANARI_WAIT, frameReady with ANARI_WAIT, anariMapFrame, rendering a
// frame again) on work that can't start until the array is unmapped gets an
// error instead of a hang (the render is dropped);
// waits that don't need that work still succeed, other threads still block
// until the array is unmapped, and the device works normally afterwards
bool testWaitWhileMapped(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto done = makeFrame(s);
  auto frame = makeFrame(s);

  anari::render(d, done);
  anari::wait(d, done);

  anari::map<float4>(d, s.colors);

  // a finished render isn't waited on
  if (anariFrameReady(d, done, ANARI_WAIT) != 1 || g_messages.numErrors()) {
    std::printf("FAIL wait while mapped: finished frame not ready\n");
    pass = false;
  }

  float b[6] = {};
  if (anariGetProperty(
          d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_NO_WAIT)
      != 1) {
    std::printf("FAIL wait while mapped: getProperty(NO_WAIT) failed\n");
    pass = false;
  }

  // another thread still blocks until the array is unmapped
  std::atomic<int> otherResult{-1};
  std::thread other([&]() {
    float ob[6] = {};
    otherResult = anariGetProperty(
        d, s.world, "bounds", ANARI_FLOAT32_BOX3, ob, sizeof(ob), ANARI_WAIT);
  });

  if (anariGetProperty(
          d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT)
      != 0) {
    std::printf("FAIL wait while mapped: getProperty(WAIT) succeeded\n");
    pass = false;
  }
  const bool propertyError = g_messages.has("mapped");
  g_messages.clear();

  anari::render(d, frame);
  if (anariFrameReady(d, frame, ANARI_WAIT) != 0) {
    std::printf("FAIL wait while mapped: frameReady(WAIT) returned ready\n");
    pass = false;
  }
  const bool readyError = g_messages.has("mapped");
  g_messages.clear();

  // rendering again has to wait on the queued render: dropped
  anari::render(d, frame);
  const bool renderError = g_messages.has("mapped");
  g_messages.clear();

  uint32_t width = 0, height = 0;
  ANARIDataType pixelType = ANARI_UNKNOWN;
  auto *pixels = anariMapFrame(
      d, frame, "channel.color", &width, &height, &pixelType);
  if (!pixels || width != IMAGE_SIZE.x || height != IMAGE_SIZE.y) {
    std::printf("FAIL wait while mapped: anariMapFrame returned no buffer\n");
    pass = false;
  }
  anariUnmapFrame(d, frame, "channel.color");
  const bool mapError = g_messages.has("mapped");

  if (!propertyError || !readyError || !renderError || !mapError) {
    std::printf("FAIL wait while mapped: missing error (%d, %d, %d, %d)\n",
        int(propertyError),
        int(readyError),
        int(renderError),
        int(mapError));
    pass = false;
  }

  std::this_thread::sleep_for(50ms);
  if (otherResult != -1) {
    std::printf("FAIL wait while mapped: other thread didn't block\n");
    pass = false;
  }

  anari::unmap(d, s.colors);
  other.join();
  if (otherResult != 1) {
    std::printf("FAIL wait while mapped: other thread's query failed\n");
    pass = false;
  }

  // the device carries on normally
  g_messages.clear();
  if (anariFrameReady(d, frame, ANARI_WAIT) != 1
      || std::isnan(uniformColor(d, frame).x)
      || anariGetProperty(
             d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT)
          != 1
      || g_messages.numErrors() != 0) {
    std::printf("FAIL wait while mapped: device unusable afterwards\n");
    pass = false;
  }

  anari::release(d, frame);
  anari::release(d, done);
  return report("wait while mapped", pass);
}

void countingCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  (*(std::atomic<int> *)userData)++;
}

void setCallback(anari::Device d,
    anari::Frame frame,
    ANARIFrameCompletionCallback callback,
    const void *userData)
{
  anari::setParameter(d, frame, "frameCompletionCallback", callback);
  anari::setParameter(
      d, frame, "frameCompletionCallbackUserData", (void *)userData);
  anari::commitParameters(d, frame);
}

// A render dropped because this thread has an array mapped and the frame's
// previous render is queued reports an error and still fires the completion
// callback once, after the queued render; waiting on the frame waits for it
bool testDroppedRenderFiresCallback(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);
  std::atomic<int> calls{0};
  setCallback(d, frame, (ANARIFrameCompletionCallback)countingCallback, &calls);

  anari::map<float4>(d, s.colors);
  anari::render(d, frame);
  anari::render(d, frame); // dropped
  const bool dropError = g_messages.has("mapped") && g_messages.numErrors();
  std::this_thread::sleep_for(50ms);
  const int callsWhileMapped = calls;
  anari::unmap(d, s.colors);
  anari::wait(d, frame);

  if (!dropError) {
    std::printf("FAIL dropped render fires callback: no error reported\n");
    pass = false;
  }
  if (callsWhileMapped != 0) {
    std::printf(
        "FAIL dropped render fires callback: %d callbacks before the queued "
        "render\n",
        callsWhileMapped);
    pass = false;
  }
  if (calls != 2) {
    std::printf(
        "FAIL dropped render fires callback: callback ran %d times for 2 "
        "renders\n",
        calls.load());
    pass = false;
  }
  anari::release(d, frame);
  return report("dropped render fires callback", pass);
}

struct GateCallbackState
{
  std::atomic<bool> entered{false};
  std::atomic<bool> open{false};
};

// Signals that it runs, then holds the worker until the gate opens
void gateCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  auto *cb = (GateCallbackState *)userData;
  cb->entered = true;
  while (!cb->open)
    std::this_thread::sleep_for(1ms);
}

// Waiting, with an array mapped, on a frame whose dropped render's callback
// is queued behind another frame's render reports an error instead of a hang
// (that render can't start until the array is unmapped)
bool testWaitOnDroppedBehindOtherRender(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto a = makeFrame(s);
  auto c = makeFrame(s);
  GateCallbackState gate;
  setCallback(d, a, (ANARIFrameCompletionCallback)gateCallback, &gate);

  anari::map<float4>(d, s.colors);
  anari::render(d, a);
  anari::render(d, c);
  anari::render(d, a); // dropped: its callback is queued behind c's render
  anari::unmap(d, s.colors);

  // a's render is done and its callback holds the worker, ahead of c
  while (!gate.entered)
    std::this_thread::sleep_for(1ms);
  anari::map<float4>(d, s.colors);
  gate.open = true;
  g_messages.clear();
  const int ready = anariFrameReady(d, a, ANARI_WAIT);
  const size_t errors = g_messages.numErrors();
  anari::unmap(d, s.colors);
  anari::wait(d, a);
  anari::wait(d, c);

  if (ready != 0 || errors == 0) {
    std::printf(
        "FAIL wait on dropped behind other render: frameReady returned %d "
        "with %zu errors\n",
        ready,
        errors);
    pass = false;
  }
  anari::release(d, a);
  anari::release(d, c);
  return report("wait on dropped render behind another render", pass);
}

struct CrossFrameRenderState
{
  anari::Device device{nullptr};
  anari::Frame other{nullptr};
};

void crossFrameRenderCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  auto *cb = (CrossFrameRenderState *)userData;
  // the other frame's render is queued behind the one running this callback
  anariRenderFrame(cb->device, cb->other);
}

// Rendering, from a completion callback, a frame whose render is queued
// behind it reports an error, drops the render and still fires that frame's
// completion callback once for it
bool testCrossFrameDroppedRenderFiresCallback(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto a = makeFrame(s);
  auto b = makeFrame(s);

  CrossFrameRenderState cb;
  cb.device = d;
  cb.other = b;
  setCallback(
      d, a, (ANARIFrameCompletionCallback)crossFrameRenderCallback, &cb);
  std::atomic<int> bCalls{0};
  setCallback(d, b, (ANARIFrameCompletionCallback)countingCallback, &bCalls);

  // a mapped array holds off both renders until both are queued
  anari::map<float4>(d, s.colors);
  anari::render(d, a);
  anari::render(d, b);
  anari::unmap(d, s.colors);
  anari::wait(d, a);
  anari::wait(d, b);

  if (!g_messages.has("deadlock") || g_messages.numErrors() == 0) {
    std::printf("FAIL cross frame dropped render: no error reported\n");
    pass = false;
  }
  if (bCalls != 2) {
    std::printf(
        "FAIL cross frame dropped render: callback ran %d times for 2 "
        "renders\n",
        bCalls.load());
    pass = false;
  }
  anari::release(d, a);
  anari::release(d, b);
  return report("cross frame dropped render fires callback", pass);
}

struct MappedWaitCallbackState
{
  anari::Device device{nullptr};
  anari::Array1D colors{nullptr};
  anari::World world{nullptr};
  int gotBounds{-1};
};

void mappedWaitCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  auto *cb = (MappedWaitCallbackState *)userData;
  anariMapArray(cb->device, cb->colors);
  float b[6] = {};
  cb->gotBounds = anariGetProperty(cb->device,
      cb->world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      b,
      sizeof(b),
      ANARI_WAIT);
  anariUnmapArray(cb->device, cb->colors);
}

// The same from a completion callback, which runs on the device's worker
bool testCallbackWaitWhileMapped(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  MappedWaitCallbackState cb;
  cb.device = d;
  cb.colors = s.colors;
  cb.world = s.world;
  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)mappedWaitCallback);
  anari::setParameter(d, frame, "frameCompletionCallbackUserData", (void *)&cb);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  if (cb.gotBounds != 0 || !g_messages.has("mapped")) {
    std::printf("FAIL callback wait while mapped: getProperty returned %d\n",
        cb.gotBounds);
    pass = false;
  }
  anari::release(d, frame);
  return report("callback wait while mapped", pass);
}

// Committing a frame while this thread has an array mapped and the frame's
// render is queued doesn't wait (frame commits are only finalized on the
// worker): no error, and the commit reaches the render
bool testCommitFrameWhileMapped(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  anari::map<float4>(d, s.colors);
  anari::render(d, frame);
  anari::setParameter(d, frame, "size", uint2(8, 8));
  anari::commitParameters(d, frame);
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL commit frame while mapped: unexpected errors\n");
    pass = false;
  }
  anari::unmap(d, s.colors);
  anari::wait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.width != 8 || fb.height != 8) {
    std::printf(
        "FAIL commit frame while mapped: size %ux%u\n", fb.width, fb.height);
    pass = false;
  }
  anari::unmap(d, frame, "channel.color");
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL commit frame while mapped: unexpected errors\n");
    pass = false;
  }
  anari::release(d, frame);
  return report("commit frame while mapped", pass);
}

// A status callback reporting a warning from a render's commit buffer flush
// runs inside the render's device work; an ANARI_WAIT query from there joins
// that work (it can't wait for it, nor flush again) and doesn't end it early:
// arrays mapped meanwhile still wait for the render
bool testNestedDeviceWork(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  std::atomic<int> nestedResult{-1};
  std::atomic<bool> nestedDone{false};
  std::atomic<bool> callbackDone{false};
  g_statusHook = [&](const char *message) {
    if (nestedDone || !std::strstr(message, "on quad geometry"))
      return;
    float b[6] = {};
    nestedResult = anariGetProperty(
        d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT);
    nestedDone = true;
    std::this_thread::sleep_for(200ms);
    callbackDone = true;
  };

  // warns (no 'vertex.position') when the render flushes it
  auto quad = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameter(d, quad, "vertex.color", s.colors);
  anari::commitParameters(d, quad);
  anari::render(d, frame);

  const auto end = std::chrono::steady_clock::now() + 5000ms;
  while (!nestedDone && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(1ms);
  if (!nestedDone) {
    std::printf("FAIL nested device work: no nested query\n");
    pass = false;
  }
  anari::map<float4>(d, s.colors);
  if (!callbackDone) {
    std::printf("FAIL nested device work: mapped during the render\n");
    pass = false;
  }
  anari::unmap(d, s.colors);
  anari::wait(d, frame);
  g_statusHook = nullptr;

  if (nestedResult != 1) {
    std::printf("FAIL nested device work: nested query returned %d\n",
        nestedResult.load());
    pass = false;
  }
  if (std::isnan(uniformColor(d, frame).x)) {
    std::printf("FAIL nested device work: render didn't complete\n");
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL nested device work: unexpected errors\n");
    pass = false;
  }
  anari::release(d, quad);
  anari::release(d, frame);
  return report("nested device work", pass);
}

// A status callback reporting from a render's flush may release (or unmap)
// arrays while another thread's map waits for that render to end
bool testCallbackReleasesDuringMapWait(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);
  auto array = anari::newArray1D(d, ANARI_FLOAT32, 4);

  std::atomic<bool> entered{false};
  std::atomic<bool> callbackDone{false};
  g_statusHook = [&](const char *message) {
    if (entered || !std::strstr(message, "on quad geometry"))
      return;
    entered = true;
    // let the app thread start waiting to map
    std::this_thread::sleep_for(100ms);
    anari::release(d, array);
    callbackDone = true;
  };

  // warns (no 'vertex.position') when the render flushes it
  auto quad = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameter(d, quad, "vertex.color", s.colors);
  anari::commitParameters(d, quad);
  anari::render(d, frame);

  const auto end = std::chrono::steady_clock::now() + 5000ms;
  while (!entered && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(1ms);
  anari::map<float4>(d, s.colors);
  if (!callbackDone) {
    std::printf("FAIL callback releases during map wait: mapped during render\n");
    pass = false;
  }
  anari::unmap(d, s.colors);
  anari::wait(d, frame);
  g_statusHook = nullptr;

  if (!entered) {
    std::printf("FAIL callback releases during map wait: no callback\n");
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL callback releases during map wait: unexpected errors\n");
    pass = false;
  }
  anari::release(d, quad);
  anari::release(d, frame);
  return report("callback releases during map wait", pass);
}

// One thread unmaps an array while another maps it (an app error, but not a
// silent one): either the map comes second and the array stays mapped by the
// mapping thread, or the unmap comes second and the array ends up unmapped.
// Never mapped with device work free to read it. The unmapping array notifies
// many observers, which widens the window the race needs. (Two threads mapping
// at once agree with each other the same way; that race had no visible effect.)
bool testConcurrentMapUnmap(Scene &s)
{
  constexpr int NUM_OBSERVERS = 1000;
  constexpr int NUM_ITERATIONS = 200;

  g_messages.clear();
  bool pass = true;
  auto d = s.device;

  const float3 vertices[3] = {
      {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
  auto array = anari::newArray1D(d, ANARI_FLOAT32_VEC3, 3);
  std::memcpy(anari::map<float3>(d, array), vertices, sizeof(vertices));
  anari::unmap(d, array);

  std::vector<anari::Geometry> observers;
  for (int i = 0; i < NUM_OBSERVERS; i++) {
    auto geom = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameter(d, geom, "vertex.position", array);
    anari::commitParameters(d, geom);
    observers.push_back(geom);
  }

  float b[6] = {};
  anariGetProperty(
      d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT);

  int inconsistent = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    anari::map<float3>(d, array);
    std::atomic<bool> unmapping{false};
    std::thread unmapper([&]() {
      unmapping = true;
      anari::unmap(d, array);
    });
    // map at varying times into the unmap
    while (!unmapping)
      ;
    const auto mapAt =
        std::chrono::steady_clock::now() + std::chrono::microseconds(i % 50);
    while (std::chrono::steady_clock::now() < mapAt)
      ;
    anari::map<float3>(d, array);
    unmapper.join();

    // mapped by this thread: the query is refused; unmapped: it succeeds
    g_messages.clear();
    const int queried = anariGetProperty(
        d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT);
    g_messages.clear();
    anari::unmap(d, array);
    const bool wasMapped = !g_messages.has("unmapped again");
    if ((queried == 1) == wasMapped)
      inconsistent++;
  }
  g_messages.clear();

  if (inconsistent != 0) {
    std::printf("FAIL concurrent map and unmap: %d of %d inconsistent\n",
        inconsistent,
        NUM_ITERATIONS);
    pass = false;
  }

  for (auto geom : observers)
    anari::release(d, geom);
  anari::release(d, array);
  return report("concurrent map and unmap", pass);
}

// Releasing an array that's still mapped unmaps it (with an ERROR): the
// releasing thread no longer holds a mapping and renders aren't held off
bool testReleaseWhileMapped(Scene &s)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);

  auto array = anari::newArray1D(d, ANARI_FLOAT32, 4);
  anari::map<float>(d, array);
  anari::release(d, array);

  // this thread doesn't count as holding a mapping
  float b[6] = {};
  if (anariGetProperty(
          d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT)
      != 1) {
    std::printf("FAIL release while mapped: getProperty(WAIT) refused\n");
    pass = false;
  }

  // renders aren't held off
  anari::render(d, frame);
  if (!readyWithin(d, frame, 5000ms)) {
    std::printf("FAIL release while mapped: render never ran\n");
    pass = false;
  }
  // exactly one ERROR: the release's
  if (g_messages.numErrors() != 1
      || !g_messages.hasError("released while mapped")) {
    std::printf("FAIL release while mapped: expected exactly the release's "
                "ERROR\n");
    pass = false;
  }
  anari::wait(d, frame);
  anari::release(d, frame);
  return report("release while mapped", pass);
}

// A frame whose render finished but was never waited on is destroyed on
// release, not reported as leaked at device teardown
bool testReleasedFrameNotLeaked(anari::Library library)
{
  auto d = anari::newDevice(library, "default");
  auto s = makeScene(d);
  auto frame = makeFrame(s);
  anari::render(d, frame);
  std::this_thread::sleep_for(100ms);
  anari::release(d, frame);
  releaseScene(s);

  g_messages.clear();
  anari::release(d, d);
  const bool pass = !g_messages.has("leaked");
  if (!pass)
    std::printf("FAIL released frame: leaked objects at device teardown\n");
  return report("released frame not leaked", pass);
}

// An app thread keeps mapping the vertex colors (alternating red and blue,
// written slowly) and positions (alternating two sizes, both covering the
// view) while another queries the world's bounds, with and without waiting,
// and frames render. No render sees a half-written color array: every frame
// is uniformly red or blue.
bool testStress(Scene &s, int numFrames)
{
  g_messages.clear();
  bool pass = true;
  auto d = s.device;
  auto frame = makeFrame(s);
  std::atomic<bool> done{false};

  std::thread writer([&]() {
    for (int i = 0; !done; i++) {
      auto *c = anari::map<float4>(d, s.colors);
      const float4 color = (i & 1) ? BLUE : RED;
      for (int v = 0; v < 4; v++) {
        c[v] = color;
        std::this_thread::yield();
      }
      anari::unmap(d, s.colors);
      if (i % 3 == 0)
        s.setPositions((i & 2) ? 2.f : 3.f);
    }
  });

  std::atomic<int> badBounds{0};
  std::thread bounds([&]() {
    while (!done) {
      float b[6] = {};
      const bool wait = anariGetProperty(
          d, s.world, "bounds", ANARI_FLOAT32_BOX3, b, sizeof(b), ANARI_WAIT);
      if (!wait || !(b[0] <= -2.f && b[3] >= 2.f))
        badBounds++;
      anariGetProperty(d,
          s.world,
          "bounds",
          ANARI_FLOAT32_BOX3,
          b,
          sizeof(b),
          ANARI_NO_WAIT);
    }
  });

  int torn = 0;
  for (int i = 0; i < numFrames; i++) {
    anari::render(d, frame);
    anari::wait(d, frame);
    const float4 c = uniformColor(d, frame);
    if (!near(c, RED) && !near(c, BLUE)) {
      if (torn++ == 0) {
        std::printf("FAIL stress: frame %d saw (%f, %f, %f, %f)\n",
            i,
            c.x,
            c.y,
            c.z,
            c.w);
      }
    }
  }
  done = true;
  writer.join();
  bounds.join();

  if (torn != 0) {
    std::printf("FAIL stress: %d of %d frames torn\n", torn, numFrames);
    pass = false;
  }
  if (badBounds != 0) {
    std::printf("FAIL stress: %d bad bounds queries\n", badBounds.load());
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL stress: unexpected errors\n");
    pass = false;
  }
  anari::release(d, frame);
  return report("stress", pass);
}

} // namespace

int main(int argc, const char **argv)
{
  // results show up even if a test deadlocks and times out
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const int numFrames = argc > 1 ? std::atoi(argv[1]) : 500;

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }

  bool pass = testExtensionDeclared(library);

  auto d = anari::newDevice(library, "default");
  auto s = makeScene(d);

  pass &= testFrameWaitsForMappedArray(s);
  pass &= testUnbalancedMapping(s);
  pass &= testCallbackMapsArray(s);
  pass &= testCrossFrameWait(s);
  pass &= testWaitPropertyAfterRender(s);
  pass &= testWaitWhileMapped(s);
  pass &= testDroppedRenderFiresCallback(s);
  pass &= testCrossFrameDroppedRenderFiresCallback(s);
  pass &= testWaitOnDroppedBehindOtherRender(s);
  pass &= testCallbackWaitWhileMapped(s);
  pass &= testCommitFrameWhileMapped(s);
  pass &= testNestedDeviceWork(s);
  pass &= testCallbackReleasesDuringMapWait(s);
  pass &= testConcurrentMapUnmap(s);
  pass &= testReleaseWhileMapped(s);
  pass &= testStress(s, numFrames);

  releaseScene(s);
  anari::release(d, d);

  pass &= testReleasedFrameNotLeaked(library);

  anari::unloadLibrary(library);

  return pass ? 0 : 1;
}
