// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Frame robustness: re-rendering an unchanged frame is skipped, frames with a
// zero dimension or missing objects render nothing (clearing the color
// channel) instead of crashing, skipped and incomplete renders still fire the
// completion callback, rendering a frame from its own completion callback
// warns instead of deadlocking, a world committed again with surfaces
// removed or added back renders what it holds, and a renderer of an unknown
// subtype is never valid, so its frame renders nothing.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {8, 8};

// Status messages at WARNING severity or worse, collected per test
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

  bool hasWarning(const char *text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &m : warnings) {
      if (m.find(text) != std::string::npos)
        return true;
    }
    return false;
  }

  size_t numErrors()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return errors.size();
  }
} g_messages;

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
}

// A quad covering the left half of an orthographic camera's [-1, 1]^2 view
anari::Surface makeSurface(anari::Device d)
{
  const std::vector<float3> verts = {{-0.9f, -0.9f, 0.f},
      {-0.1f, -0.9f, 0.f},
      {-0.1f, 0.9f, 0.f},
      {-0.9f, 0.9f, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};

  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

// A world of the quad makeSurface() makes
anari::World makeWorld(anari::Device d)
{
  auto surface = makeSurface(d);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::commitParameters(d, camera);
  return camera;
}

struct Scene
{
  anari::World world;
  anari::Camera camera;
  anari::Renderer renderer;
};

anari::Frame makeFrame(anari::Device d, const Scene &s, uint2 size)
{
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "world", s.world);
  anari::setParameter(d, frame, "camera", s.camera);
  anari::setParameter(d, frame, "renderer", s.renderer);
  anari::commitParameters(d, frame);
  return frame;
}

void renderAndWait(anari::Device d, anari::Frame frame)
{
  anari::render(d, frame);
  anari::wait(d, frame);
}

const float4 SENTINEL = {-1.f, -2.f, -3.f, -4.f};
constexpr size_t MISS_PIXEL = IMAGE_SIZE.x - 1; // bottom right, right half

// Overwrites every pixel of the mapped color channel with 'value'; a render
// that actually runs replaces them all
void fillColor(anari::Device d, anari::Frame frame, float4 value)
{
  auto fb = anari::map<float4>(d, frame, "channel.color");
  auto *pixels = const_cast<float4 *>(fb.data);
  for (size_t i = 0; pixels && i < size_t(fb.width) * fb.height; i++)
    pixels[i] = value;
  anari::unmap(d, frame, "channel.color");
}

float4 colorAt(anari::Device d, anari::Frame frame, size_t i)
{
  float4 c(NAN);
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && i < size_t(fb.width) * fb.height)
    c = fb.data[i];
  anari::unmap(d, frame, "channel.color");
  return c;
}

bool report(const char *test, bool pass)
{
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// A frame with no object finalized since its last render isn't rendered
// again; any change renders it
bool testSkipUnchanged(anari::Device d, const Scene &s, anari::Frame frame)
{
  g_messages.clear();
  bool pass = true;

  renderAndWait(d, frame);
  fillColor(d, frame, SENTINEL);
  renderAndWait(d, frame);
  if (colorAt(d, frame, MISS_PIXEL) != SENTINEL) {
    std::printf("FAIL skip unchanged: unchanged frame was rendered again\n");
    pass = false;
  }

  const float4 bg(0.25f, 0.5f, 0.75f, 1.f);
  anari::setParameter(d, s.renderer, "background", bg);
  anari::commitParameters(d, s.renderer);
  renderAndWait(d, frame);
  const float4 c = colorAt(d, frame, MISS_PIXEL);
  if (c != bg) {
    std::printf(
        "FAIL skip unchanged: changed frame not rendered, got "
        "(%f, %f, %f, %f)\n",
        c.x,
        c.y,
        c.z,
        c.w);
    pass = false;
  }

  if (g_messages.numErrors() != 0) {
    std::printf("FAIL skip unchanged: unexpected errors\n");
    pass = false;
  }
  return report("skip unchanged", pass);
}

// Resizing a frame to one with a zero dimension warns at commit; rendering it
// reports an error and it maps as empty
bool testInvalidSize(
    anari::Device d, anari::Frame frame, uint2 size, const char *test)
{
  g_messages.clear();
  bool pass = true;

  anari::setParameter(d, frame, "size", size);
  anari::commitParameters(d, frame);
  renderAndWait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.width * fb.height != 0) {
    std::printf("FAIL %s: mapped %ux%u pixels\n", test, fb.width, fb.height);
    pass = false;
  }
  anari::unmap(d, frame, "channel.color");

  if (!g_messages.hasWarning("invalid frame dimensions")) {
    std::printf("FAIL %s: no 'invalid frame dimensions' warning\n", test);
    pass = false;
  }
  if (g_messages.numErrors() == 0) {
    std::printf("FAIL %s: rendering reported no error\n", test);
    pass = false;
  }
  return report(test, pass);
}

// An incomplete frame reports an error and renders a cleared color channel
bool testIncompleteClears(anari::Device d, anari::Frame frame)
{
  g_messages.clear();
  bool pass = true;

  renderAndWait(d, frame);
  fillColor(d, frame, SENTINEL);
  anari::unsetParameter(d, frame, "world");
  anari::commitParameters(d, frame);
  renderAndWait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y) {
    std::printf("FAIL incomplete frame: could not map channel.color\n");
    pass = false;
  }
  for (size_t i = 0; pass && i < size_t(fb.width) * fb.height; i++) {
    if (fb.data[i] != float4(0.f)) {
      std::printf("FAIL incomplete frame: pixel %zu not cleared\n", i);
      pass = false;
    }
  }
  anari::unmap(d, frame, "channel.color");

  if (g_messages.numErrors() == 0) {
    std::printf("FAIL incomplete frame: rendering reported no error\n");
    pass = false;
  }
  return report("incomplete frame clears color", pass);
}

// A renderer of an unknown subtype is never valid (as for other object
// types; 'default' is the spec's alias of 'preview'): its frame reports an
// error and renders a cleared color channel
bool testUnknownRenderer(anari::Device d, anari::Frame frame)
{
  g_messages.clear();
  bool pass = true;

  renderAndWait(d, frame);
  fillColor(d, frame, SENTINEL);
  auto unknown = anari::newObject<anari::Renderer>(d, "noSuchRenderer");
  anari::commitParameters(d, unknown);
  anari::setParameter(d, frame, "renderer", unknown);
  anari::commitParameters(d, frame);
  renderAndWait(d, frame);

  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y) {
    std::printf("FAIL unknown renderer: could not map channel.color\n");
    pass = false;
  }
  for (size_t i = 0; pass && i < size_t(fb.width) * fb.height; i++) {
    if (fb.data[i] != float4(0.f)) {
      std::printf("FAIL unknown renderer: pixel %zu not cleared\n", i);
      pass = false;
    }
  }
  anari::unmap(d, frame, "channel.color");

  if (g_messages.numErrors() == 0) {
    std::printf("FAIL unknown renderer: rendering reported no error\n");
    pass = false;
  }
  anari::release(d, unknown);
  return report("unknown renderer subtype makes the frame invalid", pass);
}

struct CallbackState
{
  anari::Device device{nullptr};
  int calls{0};
  int readyNoWait{-1};
  int readyWait{-1};
};

void countingCallback(const void *userData, ANARIDevice, ANARIFrame)
{
  ((CallbackState *)userData)->calls++;
}

// A render skipped because nothing changed fires the completion callback
// once, like any other render, without rendering again
bool testSkipFiresCallback(anari::Device d, anari::Frame frame)
{
  g_messages.clear();
  bool pass = true;
  CallbackState cb;

  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)countingCallback);
  anari::setParameter(d, frame, "frameCompletionCallbackUserData", (void *)&cb);
  anari::commitParameters(d, frame);

  renderAndWait(d, frame);
  fillColor(d, frame, SENTINEL);
  renderAndWait(d, frame);

  if (cb.calls != 2) {
    std::printf(
        "FAIL skip fires callback: callback ran %d times for 2 renders\n",
        cb.calls);
    pass = false;
  }
  if (colorAt(d, frame, MISS_PIXEL) != SENTINEL) {
    std::printf(
        "FAIL skip fires callback: unchanged frame was rendered again\n");
    pass = false;
  }
  float duration = -1.f;
  if (!anari::getProperty(d, frame, "duration", duration, ANARI_WAIT)
      || !(duration >= 0.f)) {
    std::printf("FAIL skip fires callback: bad duration %f\n", duration);
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL skip fires callback: unexpected errors\n");
    pass = false;
  }
  return report("skip fires callback", pass);
}

// An incomplete frame reports an error and still fires the completion
// callback once per render
bool testIncompleteFiresCallback(anari::Device d, anari::Frame frame)
{
  g_messages.clear();
  bool pass = true;
  CallbackState cb;

  anari::unsetParameter(d, frame, "world");
  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)countingCallback);
  anari::setParameter(d, frame, "frameCompletionCallbackUserData", (void *)&cb);
  anari::commitParameters(d, frame);

  renderAndWait(d, frame);
  if (cb.calls != 1) {
    std::printf(
        "FAIL incomplete frame fires callback: callback ran %d times for 1 "
        "render\n",
        cb.calls);
    pass = false;
  }
  if (g_messages.numErrors() == 0) {
    std::printf(
        "FAIL incomplete frame fires callback: rendering reported no error\n");
    pass = false;
  }
  return report("incomplete frame fires callback", pass);
}

void reenteringCallback(const void *userData, ANARIDevice, ANARIFrame frame)
{
  auto *cb = (CallbackState *)userData;
  cb->calls++;
  // Each of these would wait on the job running this callback
  anariRenderFrame(cb->device, frame);
  cb->readyNoWait = anariFrameReady(cb->device, frame, ANARI_NO_WAIT);
  cb->readyWait = anariFrameReady(cb->device, frame, ANARI_WAIT);
}

// Rendering a frame from its own completion callback warns and returns;
// waiting on it from there returns immediately. The second render is skipped
// as unchanged, so the callback also reenters from the skip path
bool testCallbackReentry(anari::Device d, anari::Frame frame, CallbackState &cb)
{
  g_messages.clear();
  bool pass = true;

  cb.device = d;
  anari::setParameter(d,
      frame,
      "frameCompletionCallback",
      (ANARIFrameCompletionCallback)reenteringCallback);
  anari::setParameter(d, frame, "frameCompletionCallbackUserData", (void *)&cb);
  anari::commitParameters(d, frame);
  renderAndWait(d, frame);
  fillColor(d, frame, SENTINEL);
  renderAndWait(d, frame);

  if (colorAt(d, frame, MISS_PIXEL) != SENTINEL) {
    std::printf("FAIL callback reentry: second render was not skipped\n");
    pass = false;
  }
  if (cb.calls != 2) {
    std::printf("FAIL callback reentry: callback ran %d times\n", cb.calls);
    pass = false;
  }
  if (cb.readyNoWait != 1 || cb.readyWait != 1) {
    std::printf("FAIL callback reentry: frameReady() returned %d, %d\n",
        cb.readyNoWait,
        cb.readyWait);
    pass = false;
  }
  if (!g_messages.hasWarning("completion callback")) {
    std::printf("FAIL callback reentry: no warning\n");
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL callback reentry: unexpected errors\n");
    pass = false;
  }
  return report("callback reentry", pass);
}

// A world committed again, with no other object changed, renders what it
// holds: the background once its surfaces are removed, the quad once they
// are set back
bool testWorldRecommit(anari::Device d, const Scene &s)
{
  g_messages.clear();

  auto surface = makeSurface(d);
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  Scene recommitted = s;
  recommitted.world = world;
  auto frame = makeFrame(d, recommitted, IMAGE_SIZE);
  // left half, on the quad
  const size_t quadPixel = IMAGE_SIZE.x * (IMAGE_SIZE.y / 2) + 1;

  renderAndWait(d, frame);
  const float4 quad = colorAt(d, frame, quadPixel);
  const float4 background = colorAt(d, frame, MISS_PIXEL);

  anari::unsetParameter(d, world, "surface");
  anari::commitParameters(d, world);
  renderAndWait(d, frame);
  const float4 removed = colorAt(d, frame, quadPixel);

  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  renderAndWait(d, frame);
  const float4 setBack = colorAt(d, frame, quadPixel);

  anari::release(d, frame);
  anari::release(d, world);
  anari::release(d, surface);

  bool pass = true;
  if (quad == background) {
    std::printf("FAIL world recommit: the quad doesn't show\n");
    pass = false;
  }
  if (removed != background) {
    std::printf("FAIL world recommit: removed surfaces still render\n");
    pass = false;
  }
  if (setBack != quad) {
    std::printf("FAIL world recommit: surfaces set back don't render\n");
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL world recommit: unexpected errors\n");
    pass = false;
  }
  return report("world recommit", pass);
}

} // namespace

int main()
{
  // results show up even if the callback test deadlocks and times out
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  Scene s;
  s.world = makeWorld(d);
  s.camera = makeCamera(d);
  s.renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::commitParameters(d, s.renderer);

  // each test gets a new frame, created after the previous tests rendered
  auto withNewFrame = [&](auto &&test) {
    auto frame = makeFrame(d, s, IMAGE_SIZE);
    const bool pass = test(frame);
    anari::release(d, frame);
    return pass;
  };
  CallbackState cb;

  bool pass = true;
  pass &= withNewFrame([&](auto f) { return testSkipUnchanged(d, s, f); });
  pass &= withNewFrame([&](auto f) {
    return testInvalidSize(d, f, uint2(0, 0), "zero size frame");
  });
  pass &= withNewFrame([&](auto f) {
    return testInvalidSize(d, f, uint2(IMAGE_SIZE.x, 0), "zero height frame");
  });
  pass &= withNewFrame([&](auto f) { return testIncompleteClears(d, f); });
  pass &= withNewFrame([&](auto f) { return testUnknownRenderer(d, f); });
  pass &= withNewFrame([&](auto f) { return testSkipFiresCallback(d, f); });
  pass &=
      withNewFrame([&](auto f) { return testIncompleteFiresCallback(d, f); });
  pass &= withNewFrame([&](auto f) { return testCallbackReentry(d, f, cb); });
  pass &= testWorldRecommit(d, s);

  anari::release(d, s.renderer);
  anari::release(d, s.camera);
  anari::release(d, s.world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  return pass ? 0 : 1;
}
