// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Shared arrays the app releases while the device still uses them: helium
// copies them (privatizes), and halcyon re-points the geometry at the copy, so
// the app may then overwrite or free its memory without changing the image.
// The release waits for a render in progress, which may still be reading the
// app's memory, but never waits while holding the lock a status callback
// releasing arrays during that render needs. Hangs show up as a ctest timeout.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
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

constexpr uint2 IMAGE_SIZE = {32, 32};
// Junk elements before 'begin' in the offset arrays of
// testReleaseThenOverwrite()
constexpr size_t PREFIX = 2;

// Status messages at WARNING severity or worse
struct Messages
{
  std::mutex mutex;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex);
    errors.clear();
    warnings.clear();
  }

  void add(ANARIStatusSeverity severity, const char *message)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (severity <= ANARI_SEVERITY_ERROR)
      errors.push_back(message);
    else
      warnings.push_back(message);
  }

  // Number of warnings (not errors) containing 'text'
  size_t countWarnings(const char *text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    size_t n = 0;
    for (auto &w : warnings)
      n += w.find(text) != std::string::npos;
    return n;
  }

  size_t numErrors()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return errors.size();
  }

  // True if an error contains 'text'
  bool hasError(const char *text)
  {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto &e : errors) {
      if (e.find(text) != std::string::npos)
        return true;
    }
    return false;
  }
} g_messages;

// Called with every status message, on the thread that reported it
std::function<void(ANARIStatusSeverity, const char *)> g_statusHook;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI] %s\n", message);
    g_messages.add(severity, message);
  }
  if (g_statusHook)
    g_statusHook(severity, message);
}

bool report(const char *test, bool pass)
{
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// A triangle mesh (a square in the left half of the view) and a quad (a
// square in the right half), whose 'vertex.position' and 'primitive.index'
// share the app memory below. Vertex colors (copied by the device) differ per
// vertex, so the image depends on the indices. With a 'prefix', each array
// starts with that many junk elements (off-screen positions, degenerate
// primitives) before its 'begin', so the image is the same.
struct Meshes
{
  size_t prefix;
  std::vector<float3> triPositions = {{-0.9f, -0.4f, 0.f},
      {-0.1f, -0.4f, 0.f},
      {-0.1f, 0.4f, 0.f},
      {-0.9f, 0.4f, 0.f}};
  std::vector<uint3> triIndices = {{0, 1, 2}, {0, 2, 3}};
  std::vector<float3> quadPositions = {{0.1f, -0.4f, 0.f},
      {0.9f, -0.4f, 0.f},
      {0.9f, 0.4f, 0.f},
      {0.1f, 0.4f, 0.f}};
  std::vector<uint4> quadIndices = {{0, 1, 2, 3}};

  // the shared arrays, in the order above
  anari::Array1D arrays[4] = {};

  explicit Meshes(size_t prefix = 0) : prefix(prefix)
  {
    const float3 offScreen(5.f, 5.f, 0.f);
    triPositions.insert(triPositions.begin(), prefix, offScreen);
    quadPositions.insert(quadPositions.begin(), prefix, offScreen);
    triIndices.insert(triIndices.begin(), prefix, uint3(1u));
    quadIndices.insert(quadIndices.begin(), prefix, uint4(1u));
  }

  void releaseArrays(anari::Device d)
  {
    for (auto &a : arrays) {
      anari::release(d, a);
      a = nullptr;
    }
  }

  // What an app may do once it released the arrays: different, still valid
  // positions and indices, then free
  void overwriteAppMemory()
  {
    for (auto *p : {&triPositions, &quadPositions}) {
      for (auto &v : *p)
        v = float3(0.5f, 0.5f, 0.f) - v;
    }
    for (auto &t : triIndices)
      t = uint3(t.z, t.y, t.x);
    for (auto &q : quadIndices)
      q = uint4(q.z, q.w, q.x, q.y);
  }

  void freeAppMemory()
  {
    overwriteAppMemory();
    for (auto *p : {&triPositions, &quadPositions})
      std::vector<float3>().swap(*p);
    std::vector<uint3>().swap(triIndices);
    std::vector<uint4>().swap(quadIndices);
  }
};

const std::vector<float3> VERTEX_COLORS = {
    {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}};

anari::Surface makeSurface(anari::Device d,
    const char *subtype,
    anari::Array1D positions,
    anari::Array1D indices)
{
  auto geom = anari::newObject<anari::Geometry>(d, subtype);
  anari::setParameter(d, geom, "vertex.position", positions);
  anari::setParameter(d, geom, "primitive.index", indices);
  anari::setParameterArray1D(
      d, geom, "vertex.color", VERTEX_COLORS.data(), VERTEX_COLORS.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

anari::World makeWorld(anari::Device d, Meshes &m)
{
  m.arrays[0] = anari::newArray1D(d, m.triPositions.data(), m.prefix + 4);
  m.arrays[1] = anari::newArray1D(d, m.triIndices.data(), m.prefix + 2);
  m.arrays[2] = anari::newArray1D(d, m.quadPositions.data(), m.prefix + 4);
  m.arrays[3] = anari::newArray1D(d, m.quadIndices.data(), m.prefix + 1);
  if (m.prefix != 0) {
    for (auto a : m.arrays) {
      anari::setParameter(d, a, "begin", uint64_t(m.prefix));
      anari::commitParameters(d, a);
    }
  }

  anari::Surface surfaces[] = {
      makeSurface(d, "triangle", m.arrays[0], m.arrays[1]),
      makeSurface(d, "quad", m.arrays[2], m.arrays[3])};
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", surfaces, 2);
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

struct View
{
  anari::Camera camera{nullptr};
  anari::Renderer renderer{nullptr};
};

View makeView(anari::Device d)
{
  View v;
  v.camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, v.camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, v.camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, v.camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, v.camera, "height", 2.f);
  anari::setParameter(d, v.camera, "aspect", 1.f);
  anari::commitParameters(d, v.camera);

  // the interpolated vertex color, unshaded
  v.renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, v.renderer, "mode", "geometry.color");
  anari::setParameter(d, v.renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::commitParameters(d, v.renderer);
  return v;
}

// A new frame for 'world', so each render runs (no frame skips it)
anari::Frame makeFrame(anari::Device d, const View &v, anari::World world)
{
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "world", world);
  anari::setParameter(d, frame, "camera", v.camera);
  anari::setParameter(d, frame, "renderer", v.renderer);
  anari::commitParameters(d, frame);
  return frame;
}

std::vector<float4> readColor(anari::Device d, anari::Frame frame)
{
  auto fb = anari::map<float4>(d, frame, "channel.color");
  std::vector<float4> pixels(fb.data, fb.data + size_t(fb.width) * fb.height);
  anari::unmap(d, frame, "channel.color");
  return pixels;
}

std::vector<float4> render(anari::Device d, const View &v, anari::World world)
{
  auto frame = makeFrame(d, v, world);
  anari::render(d, frame);
  anari::wait(d, frame);
  auto pixels = readColor(d, frame);
  anari::release(d, frame);
  return pixels;
}

// Pixels that differ from 'expected'
size_t countDifferences(
    const std::vector<float4> &expected, const std::vector<float4> &actual)
{
  if (expected.size() != actual.size())
    return std::max(expected.size(), actual.size());
  size_t n = 0;
  for (size_t i = 0; i < expected.size(); i++) {
    const auto &a = expected[i];
    const auto &b = actual[i];
    if (std::fabs(a.x - b.x) > 1e-5f || std::fabs(a.y - b.y) > 1e-5f
        || std::fabs(a.z - b.z) > 1e-5f || std::fabs(a.w - b.w) > 1e-5f)
      n++;
  }
  return n;
}

// The image shows both meshes: colored, not background, pixels in both halves
bool showsBothMeshes(const std::vector<float4> &pixels)
{
  bool left = false, right = false;
  for (size_t i = 0; i < pixels.size(); i++) {
    const auto &p = pixels[i];
    if (p.x + p.y + p.z > 0.f)
      ((i % IMAGE_SIZE.x) < IMAGE_SIZE.x / 2 ? left : right) = true;
  }
  return left && right;
}

// Waits up to 5 s for 'flag'
bool waitFor(const std::atomic<bool> &flag)
{
  const auto end = std::chrono::steady_clock::now() + 5000ms;
  while (!flag && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(1ms);
  return flag;
}

// Releasing the shared arrays, then overwriting and freeing the app memory,
// leaves the image as it was. With a 'prefix' before each array's 'begin',
// the image is the one without; helium's own privatize() copies only
// size() elements from the start of the buffer, dropping the end of the range
// (halcyon's copies the whole buffer).
bool testReleaseThenOverwrite(anari::Device d, const View &v, size_t prefix)
{
  const char *name = prefix ? "release offset arrays then overwrite"
                            : "release then overwrite";
  g_messages.clear();
  bool pass = true;

  Meshes m(prefix);
  auto world = makeWorld(d, m);
  const auto expected = render(d, v, world);
  if (!showsBothMeshes(expected)) {
    std::printf("FAIL %s: meshes not rendered\n", name);
    pass = false;
  }
  if (prefix != 0) {
    Meshes plain;
    auto plainWorld = makeWorld(d, plain);
    const size_t differ = countDifferences(render(d, v, plainWorld), expected);
    if (differ != 0) {
      std::printf("FAIL %s: %zu pixels differ from the arrays without prefix\n",
          name,
          differ);
      pass = false;
    }
    plain.releaseArrays(d);
    anari::release(d, plainWorld);
  }

  m.releaseArrays(d);
  m.overwriteAppMemory();
  const size_t overwritten = countDifferences(expected, render(d, v, world));
  if (overwritten != 0) {
    std::printf(
        "FAIL %s: %zu pixels changed after overwriting\n", name, overwritten);
    pass = false;
  }

  m.freeAppMemory();
  const size_t freed = countDifferences(expected, render(d, v, world));
  if (freed != 0) {
    std::printf("FAIL %s: %zu pixels changed after freeing\n", name, freed);
    pass = false;
  }

  if (g_messages.numErrors() != 0) {
    std::printf("FAIL %s: unexpected errors\n", name);
    pass = false;
  }
  anari::release(d, world);
  return report(name, pass);
}

// Spheres (top half) and cylinders (bottom half) whose 'primitive.index'
// shares the app memory in 'indices'. The ISPC hooks read those indices in
// place to interpolate the vertex colors, while Embree's buffers are copies,
// so an overwrite of the indices would change only the colors.
anari::Surface makeIndexedSurface(anari::Device d,
    const char *subtype,
    const std::vector<float3> &positions,
    anari::Array1D indices,
    float radius)
{
  auto geom = anari::newObject<anari::Geometry>(d, subtype);
  anari::setParameterArray1D(
      d, geom, "vertex.position", positions.data(), positions.size());
  anari::setParameter(d, geom, "primitive.index", indices);
  anari::setParameter(d, geom, "radius", radius);
  anari::setParameterArray1D(
      d, geom, "vertex.color", VERTEX_COLORS.data(), VERTEX_COLORS.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

// The indices of the spheres and cylinders below, in app memory
struct SharedIndices
{
  std::vector<uint32_t> spheres = {0, 1, 2, 3};
  std::vector<uint2> cylinders = {{0, 1}, {2, 3}};

  // Different, still valid indices. They permute the primitives, so a device
  // reading them consistently renders the same image; a hook reading them
  // while Embree still has the old primitives gets the wrong vertex colors.
  void overwrite()
  {
    std::reverse(spheres.begin(), spheres.end());
    for (auto &c : cylinders)
      c = uint2(c.y, c.x);
  }
};

// A world of spheres and cylinders whose 'primitive.index' arrays share 'm';
// the arrays are returned in 'arrays'
anari::World makeIndexedWorld(
    anari::Device d, SharedIndices &m, anari::Array1D arrays[2])
{
  static const std::vector<float3> spherePositions = {{-0.75f, 0.5f, 0.f},
      {-0.25f, 0.5f, 0.f},
      {0.25f, 0.5f, 0.f},
      {0.75f, 0.5f, 0.f}};
  static const std::vector<float3> cylinderPositions = {{-0.8f, -0.5f, 0.f},
      {-0.1f, -0.5f, 0.f},
      {0.1f, -0.5f, 0.f},
      {0.8f, -0.5f, 0.f}};

  arrays[0] = anari::newArray1D(d, m.spheres.data(), m.spheres.size());
  arrays[1] = anari::newArray1D(d, m.cylinders.data(), m.cylinders.size());
  anari::Surface surfaces[] = {
      makeIndexedSurface(d, "sphere", spherePositions, arrays[0], 0.2f),
      makeIndexedSurface(d, "cylinder", cylinderPositions, arrays[1], 0.15f)};
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", surfaces, 2);
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// Releasing the shared UINT32 'primitive.index' of sphere and cylinder
// geometry, whose ISPC hooks read it in place, then overwriting the app
// memory leaves the image as it was
bool testReleaseSharedIndices(anari::Device d, const View &v)
{
  const char *name = "release shared sphere and cylinder indices";
  g_messages.clear();
  bool pass = true;

  SharedIndices m;
  anari::Array1D arrays[2];
  auto world = makeIndexedWorld(d, m, arrays);
  const auto expected = render(d, v, world);

  for (auto a : arrays)
    anari::release(d, a);
  m.overwrite();
  const size_t overwritten = countDifferences(expected, render(d, v, world));
  if (overwritten != 0) {
    std::printf(
        "FAIL %s: %zu pixels changed after overwriting\n", name, overwritten);
    pass = false;
  }

  if (g_messages.numErrors() != 0) {
    std::printf("FAIL %s: unexpected errors\n", name);
    pass = false;
  }
  anari::release(d, world);
  return report(name, pass);
}

// A quad geometry that warns (no 'vertex.position') when a render's commit
// buffer flush finalizes it, so 'g_statusHook' runs inside that render
anari::Geometry makeWarningGeometry(anari::Device d)
{
  auto quad = anari::newObject<anari::Geometry>(d, "quad");
  // a parameter change, so the flush commits and finalizes it
  anari::setParameter(d, quad, "radius", 1.f);
  anari::commitParameters(d, quad);
  return quad;
}

bool isWarningGeometryMessage(const char *message)
{
  return std::strstr(message, "on quad geometry") != nullptr;
}

// A release during a render returns only after the render: the render may be
// reading the app memory the app overwrites once the release returns
bool testReleaseWaitsForRender(anari::Device d, const View &v)
{
  g_messages.clear();
  bool pass = true;

  Meshes m;
  auto world = makeWorld(d, m);
  const auto expected = render(d, v, world);

  std::atomic<bool> entered{false};
  std::atomic<bool> renderDone{false};
  g_statusHook = [&](ANARIStatusSeverity, const char *message) {
    if (entered || !isWarningGeometryMessage(message))
      return;
    entered = true;
    // the release below starts meanwhile
    std::this_thread::sleep_for(100ms);
    renderDone = true;
  };

  auto warning = makeWarningGeometry(d);
  auto frame = makeFrame(d, v, world);
  anari::render(d, frame);

  if (!waitFor(entered)) {
    std::printf("FAIL release waits for render: no callback\n");
    pass = false;
  }
  m.releaseArrays(d);
  if (!renderDone) {
    std::printf("FAIL release waits for render: returned during the render\n");
    pass = false;
  }
  m.freeAppMemory();
  anari::wait(d, frame);
  g_statusHook = nullptr;

  const size_t changed = countDifferences(expected, readColor(d, frame));
  if (changed != 0) {
    std::printf("FAIL release waits for render: %zu pixels changed\n", changed);
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL release waits for render: unexpected errors\n");
    pass = false;
  }
  anari::release(d, frame);
  anari::release(d, warning);
  anari::release(d, world);
  return report("release waits for render", pass);
}

// A status callback during a render releases shared arrays while the app
// thread's release waits for that render: neither deadlocks, and the arrays
// released from the callback are re-pointed too. The callback's releases
// can't wait for the render they're part of, so each warns that the render
// may still read the app's memory; the app thread's releases don't warn.
bool testCallbackReleasesDuringRender(anari::Device d, const View &v)
{
  g_messages.clear();
  bool pass = true;

  Meshes m;
  auto world = makeWorld(d, m);
  const auto expected = render(d, v, world);

  std::atomic<bool> entered{false};
  std::atomic<bool> callbackDone{false};
  g_statusHook = [&](ANARIStatusSeverity, const char *message) {
    if (entered || !isWarningGeometryMessage(message))
      return;
    entered = true;
    // let the app thread's release start waiting for this render
    std::this_thread::sleep_for(100ms);
    // the quad's arrays
    anari::release(d, m.arrays[2]);
    anari::release(d, m.arrays[3]);
    callbackDone = true;
  };

  auto warning = makeWarningGeometry(d);
  auto frame = makeFrame(d, v, world);
  anari::render(d, frame);

  if (!waitFor(entered)) {
    std::printf("FAIL callback releases during render: no callback\n");
    pass = false;
  }
  // the triangle mesh's arrays
  anari::release(d, m.arrays[0]);
  anari::release(d, m.arrays[1]);
  if (!callbackDone) {
    std::printf(
        "FAIL callback releases during render: returned during the render\n");
    pass = false;
  }
  anari::wait(d, frame);
  g_statusHook = nullptr;
  for (auto &a : m.arrays)
    a = nullptr;

  m.freeAppMemory();
  const size_t changed = countDifferences(expected, render(d, v, world));
  if (changed != 0) {
    std::printf(
        "FAIL callback releases during render: %zu pixels changed\n", changed);
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL callback releases during render: unexpected errors\n");
    pass = false;
  }
  const size_t warnings = g_messages.countWarnings("may still read");
  if (warnings != 2) {
    std::printf(
        "FAIL callback releases during render: %zu release warnings, "
        "expected 2\n",
        warnings);
    pass = false;
  }
  anari::release(d, frame);
  anari::release(d, warning);
  anari::release(d, world);
  return report("callback releases during render", pass);
}

// A thread with another array mapped may release shared arrays: no device
// work can be in progress while it holds the mapping, so there's nothing to
// wait for and no error
bool testReleaseWhileMapped(anari::Device d, const View &v)
{
  g_messages.clear();
  bool pass = true;

  Meshes m;
  auto world = makeWorld(d, m);
  const auto expected = render(d, v, world);

  auto other = anari::newArray1D(d, ANARI_FLOAT32, 4);
  anari::map<float>(d, other);
  m.releaseArrays(d);
  anari::unmap(d, other);
  anari::release(d, other);

  m.freeAppMemory();
  const size_t changed = countDifferences(expected, render(d, v, world));
  if (changed != 0) {
    std::printf("FAIL release while mapped: %zu pixels changed\n", changed);
    pass = false;
  }
  if (g_messages.numErrors() != 0) {
    std::printf("FAIL release while mapped: unexpected errors\n");
    pass = false;
  }
  anari::release(d, world);
  return report("release while mapped", pass);
}

// helium reports the privatize copy (a performance warning) while the release
// holds device work off. A status callback waiting on the device then can't
// wait: it gets an ERROR that names the release, not a mapped array.
bool testWaitDuringReleaseHold(anari::Device d, const View &v)
{
  g_messages.clear();
  bool pass = true;

  Meshes m;
  auto world = makeWorld(d, m);
  const auto expected = render(d, v, world);

  bool queried = false;
  int result = -1;
  g_statusHook = [&](ANARIStatusSeverity, const char *message) {
    if (queried || !std::strstr(message, "making private copy"))
      return;
    queried = true;
    float3 bounds[2];
    result = anariGetProperty(d,
        world,
        "bounds",
        ANARI_FLOAT32_BOX3,
        bounds,
        sizeof(bounds),
        ANARI_WAIT);
  };
  m.releaseArrays(d);
  g_statusHook = nullptr;

  if (!queried) {
    std::printf("FAIL wait during release hold: no privatize message\n");
    pass = false;
  }
  if (result != 0) {
    std::printf("FAIL wait during release hold: query returned %d\n", result);
    pass = false;
  }
  if (!g_messages.hasError("releasing an array")
      || g_messages.hasError("mapped")) {
    std::printf("FAIL wait during release hold: wrong or missing error\n");
    pass = false;
  }

  m.freeAppMemory();
  const size_t changed = countDifferences(expected, render(d, v, world));
  if (changed != 0) {
    std::printf("FAIL wait during release hold: %zu pixels changed\n", changed);
    pass = false;
  }
  anari::release(d, world);
  return report("wait during release hold", pass);
}

} // namespace

int main()
{
  // results show up even if a test deadlocks and times out
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }

  auto d = anari::newDevice(library, "default");
  auto v = makeView(d);

  bool pass = testReleaseThenOverwrite(d, v, 0);
  pass &= testReleaseThenOverwrite(d, v, PREFIX);
  pass &= testReleaseSharedIndices(d, v);
  pass &= testReleaseWaitsForRender(d, v);
  pass &= testCallbackReleasesDuringRender(d, v);
  pass &= testReleaseWhileMapped(d, v);
  pass &= testWaitDuringReleaseHold(d, v);

  anari::release(d, v.renderer);
  anari::release(d, v.camera);
  anari::release(d, d);
  anari::unloadLibrary(library);

  return pass ? 0 : 1;
}
