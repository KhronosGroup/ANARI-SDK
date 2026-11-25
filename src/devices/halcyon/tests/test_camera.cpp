// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders a square through perspective and orthographic cameras into
// non-square frames and checks, via the depth channel, which pixels hit it:
// without an 'aspect' parameter the camera takes the frame's aspect ratio,
// with one it uses that instead.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {

using namespace anari::math;

// Half-width of the square at z = 0. Pixel centres in the frames below land
// at 0.025 + 0.05k or 0.05 + 0.1k from the axes at z = 0, so none is closer
// than 0.025 to its edges
constexpr float HALF_WIDTH = 0.4f;

// Both cameras sit at z = 2 looking down -z and see a height of 2 at z = 0
constexpr float VIEW_HEIGHT = 2.f;

anari::World makeWorld(anari::Device d)
{
  const float h = HALF_WIDTH;
  const std::vector<float3> verts = {
      {-h, -h, 0.f}, {h, -h, 0.f}, {h, h, 0.f}, {-h, h, 0.f}};
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

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

anari::Camera makeCamera(anari::Device d, const char *type)
{
  auto camera = anari::newObject<anari::Camera>(d, type);
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  // image plane height 1 at unit distance, so 2 at the square
  anari::setParameter(d, camera, "fovy", 2.f * std::atan(0.5f));
  anari::setParameter(d, camera, "height", VIEW_HEIGHT);
  anari::commitParameters(d, camera);
  return camera;
}

anari::Frame makeFrame(
    anari::Device d, uint2 size, anari::World world, anari::Renderer renderer)
{
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", size);
  anari::setParameter(d, frame, "channel.color", ANARI_UFIXED8_RGBA_SRGB);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "world", world);
  anari::setParameter(d, frame, "renderer", renderer);
  return frame;
}

// Renders 'frame' through 'camera' and checks that exactly the pixels whose
// sample position lands on the square at the given 'aspect' hit it
bool check(anari::Device d,
    const char *test,
    anari::Frame frame,
    uint2 size,
    anari::Camera camera,
    float aspect)
{
  anari::setParameter(d, frame, "camera", camera);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  bool pass = true;
  auto fb = anari::map<float>(d, frame, "channel.depth");
  if (!fb.data || fb.width != size.x || fb.height != size.y) {
    std::printf("FAIL %s: could not map channel.depth\n", test);
    pass = false;
  }
  for (uint32_t y = 0; y < size.y && pass; y++) {
    for (uint32_t x = 0; x < size.x && pass; x++) {
      // rays go through pixel centres
      const float wx = ((x + 0.5f) / size.x - 0.5f) * VIEW_HEIGHT * aspect;
      const float wy = ((y + 0.5f) / size.y - 0.5f) * VIEW_HEIGHT;
      const bool expected =
          std::abs(wx) < HALF_WIDTH && std::abs(wy) < HALF_WIDTH;
      // hits are about 2 away; missed rays write depth FLT_MAX
      const bool hit = fb.data[y * size.x + x] < 100.f;
      if (hit != expected) {
        std::printf("FAIL %s: pixel (%u, %u) %s, expected %s\n",
            test,
            x,
            y,
            hit ? "hit" : "missed",
            expected ? "hit" : "miss");
        pass = false;
      }
    }
  }
  anari::unmap(d, frame, "channel.depth");
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// device errors (e.g. ISPC object size mismatches) fail the test
bool g_deviceError = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR)
    g_deviceError = true;
  if (severity <= ANARI_SEVERITY_WARNING)
    std::fprintf(stderr, "[ANARI] %s\n", message);
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  auto world = makeWorld(d);
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::commitParameters(d, renderer);

  const uint2 wide(40, 20);
  const uint2 tall(20, 40);
  auto wideFrame = makeFrame(d, wide, world, renderer);
  auto tallFrame = makeFrame(d, tall, world, renderer);

  bool pass = true;
  for (const char *type : {"perspective", "orthographic"}) {
    auto camera = makeCamera(d, type);
    char test[64];

    // one camera follows each frame's aspect ratio
    std::snprintf(test, sizeof(test), "%s wide frame", type);
    pass &= check(d, test, wideFrame, wide, camera, 2.f);
    std::snprintf(test, sizeof(test), "%s tall frame", type);
    pass &= check(d, test, tallFrame, tall, camera, 0.5f);

    std::snprintf(test, sizeof(test), "%s explicit aspect", type);
    anari::setParameter(d, camera, "aspect", 1.f);
    anari::commitParameters(d, camera);
    pass &= check(d, test, wideFrame, wide, camera, 1.f);

    std::snprintf(test, sizeof(test), "%s unset aspect", type);
    anari::unsetParameter(d, camera, "aspect");
    anari::commitParameters(d, camera);
    pass &= check(d, test, wideFrame, wide, camera, 2.f);

    anari::release(d, camera);
  }

  anari::release(d, wideFrame);
  anari::release(d, tallFrame);
  anari::release(d, renderer);
  anari::release(d, world);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
