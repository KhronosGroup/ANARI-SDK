// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders sphere geometry through the device and checks probe pixels of the
// float color channel in debug modes: vertex colors (not interpolated),
// primitive IDs, Embree primitive indices and geometric normals. One surface
// has indexed spheres with per-vertex radii, one of its vertices unused; the
// other is implicitly indexed with the global 'radius'. A last check renders a
// sphere without 'radius' to check its default.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};

// Indexed: spheres on vertices 2 and 0; vertex 1 isn't referenced
const std::vector<float3> INDEXED_VERTICES = {
    {-0.5f, 0.5f, 0.f}, {0.5f, 0.5f, 0.f}, {-0.5f, -0.5f, 0.f}};
const std::vector<float> INDEXED_RADII = {0.3f, 0.2f, 0.2f};
const std::vector<float3> INDEXED_COLORS = {
    {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
const std::vector<uint32_t> INDICES = {2, 0};
const std::vector<uint32_t> PRIMITIVE_IDS = {100, 101};

// Soup: one sphere per vertex, all of radius SOUP_RADIUS
const std::vector<float3> SOUP_VERTICES = {
    {0.5f, -0.2f, 0.f}, {0.5f, -0.7f, 0.f}};
const float SOUP_RADIUS = 0.15f;
const std::vector<float3> SOUP_COLORS = {{1.f, 1.f, 0.f}, {0.f, 1.f, 1.f}};

// A sphere of one of the surfaces
struct SphereRef
{
  float3 center;
  float radius;
  float3 color;
  uint32_t prim; // Embree primID
  uint32_t primitiveId; // 'primitive.id', or 'prim' without one
};

SphereRef indexedSphere(uint32_t i)
{
  const uint32_t v = INDICES[i];
  return {INDEXED_VERTICES[v],
      INDEXED_RADII[v],
      INDEXED_COLORS[v],
      i,
      PRIMITIVE_IDS[i]};
}

SphereRef soupSphere(uint32_t i)
{
  return {SOUP_VERTICES[i], SOUP_RADIUS, SOUP_COLORS[i], i, i};
}

anari::Surface makeSurface(anari::Device d, bool indexed)
{
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  const auto &verts = indexed ? INDEXED_VERTICES : SOUP_VERTICES;
  const auto &colors = indexed ? INDEXED_COLORS : SOUP_COLORS;
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "vertex.color", colors.data(), colors.size());
  if (indexed) {
    anari::setParameterArray1D(
        d, geom, "vertex.radius", INDEXED_RADII.data(), INDEXED_RADII.size());
    anari::setParameterArray1D(
        d, geom, "primitive.index", INDICES.data(), INDICES.size());
    anari::setParameterArray1D(
        d, geom, "primitive.id", PRIMITIVE_IDS.data(), PRIMITIVE_IDS.size());
  } else {
    anari::setParameter(d, geom, "radius", SOUP_RADIUS);
  }
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(1.f));
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

anari::World makeWorld(anari::Device d)
{
  anari::Surface surfaces[] = {makeSurface(d, true), makeSurface(d, false)};
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", surfaces, 2);
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// Orthographic camera looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

struct Probe
{
  const char *name;
  uint2 pixel;
  bool hit;
  SphereRef sphere;
  float3 normal; // unit normal of the sphere at the pixel's ray
};

// The camera samples pixel centres; pixelAt() is the pixel covering a world
// position, rayAt() its sample position
uint2 pixelAt(float2 pos)
{
  return uint2(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
}

float2 rayAt(uint2 px)
{
  return float2(2.f * (px.x + 0.5f) / IMAGE_SIZE.x - 1.f,
      2.f * (px.y + 0.5f) / IMAGE_SIZE.y - 1.f);
}

// Probe at the pixel covering 'offset' from the center of sphere 's'
Probe makeHitProbe(const char *name, const SphereRef &s, float2 offset)
{
  const uint2 px = pixelAt(float2(s.center.x, s.center.y) + offset);
  const float2 d = rayAt(px) - float2(s.center.x, s.center.y);
  const float r2 = s.radius * s.radius;
  const float3 n = float3(d.x, d.y, std::sqrt(r2 - linalg::dot(d, d)));
  return {name, px, true, s, n / s.radius};
}

Probe makeMissProbe(const char *name, float2 pos)
{
  return {name, pixelAt(pos), false, {}, {}};
}

const std::vector<Probe> &probes()
{
  static const std::vector<Probe> retval = {
      makeMissProbe("miss", float2(0.f, 0.9f)),
      makeMissProbe("unreferenced vertex", float2(0.5f, 0.5f)),
      makeHitProbe("indexed 0", indexedSphere(0), float2(0.f, 0.f)),
      makeHitProbe("indexed 1", indexedSphere(1), float2(0.05f, -0.05f)),
      // inside vertex 0's radius 0.3, outside vertex 2's 0.2
      makeHitProbe(
          "indexed 1, vertex radius", indexedSphere(1), float2(0.25f, 0.f)),
      makeMissProbe("indexed 0, outside vertex radius", float2(-0.5f, -0.2f)),
      makeHitProbe("soup 0", soupSphere(0), float2(0.05f, 0.05f)),
      makeHitProbe("soup 1, global radius", soupSphere(1), float2(-0.08f, 0.f)),
      makeMissProbe("soup 1, outside global radius", float2(0.5f, -0.9f)),
  };
  return retval;
}

// helide's makeRandomColor()
float3 randomColor(uint32_t i)
{
  const uint32_t mx = 13 * 17 * 43;
  const uint32_t my = 11 * 29;
  const uint32_t mz = 7 * 23 * 63;
  const uint32_t g = (i * (3 * 5 * 127) + 12312314);
  return float3((g % mx) * (1.f / (mx - 1)),
      (g % my) * (1.f / (my - 1)),
      (g % mz) * (1.f / (mz - 1)));
}

struct ModeTest
{
  const char *mode;
  std::function<float3(const Probe &)> expected;
  float tolerance;
};

const std::vector<ModeTest> MODE_TESTS = {
    {"geometry.color", [](const Probe &p) { return p.sphere.color; }, 1e-4f},
    {"primitiveId",
        [](const Probe &p) { return randomColor(p.sphere.primitiveId); },
        1e-4f},
    {"embreePrimID",
        [](const Probe &p) { return randomColor(p.sphere.prim); },
        1e-4f},
    {"Ng", [](const Probe &p) { return p.normal; }, 1e-3f},
};

bool near(float4 a, float4 b, float tolerance)
{
  const float4 d = linalg::abs(a - b);
  return d.x < tolerance && d.y < tolerance && d.z < tolerance
      && d.w < tolerance;
}

bool testMode(anari::Device d,
    anari::Frame frame,
    anari::Renderer renderer,
    const ModeTest &test)
{
  anari::setParameter(d, renderer, "mode", test.mode);
  anari::commitParameters(d, renderer);
  anari::render(d, frame);
  anari::wait(d, frame);

  bool pass = true;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (!fb.data || fb.width != IMAGE_SIZE.x || fb.height != IMAGE_SIZE.y) {
    std::printf("FAIL %s: could not map channel.color\n", test.mode);
    pass = false;
  } else {
    for (const auto &p : probes()) {
      const float4 expected =
          p.hit ? float4(test.expected(p), 1.f) : BACKGROUND;
      const float4 got = fb.data[size_t(p.pixel.y) * IMAGE_SIZE.x + p.pixel.x];
      if (!near(got, expected, test.tolerance)) {
        std::printf(
            "FAIL %s, %s: got (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
            test.mode,
            p.name,
            got.x,
            got.y,
            got.z,
            got.w,
            expected.x,
            expected.y,
            expected.z,
            expected.w);
        pass = false;
      }
    }
  }
  anari::unmap(d, frame, "channel.color");

  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test.mode);
  return pass;
}

// Without 'radius', a sphere has khr_geometry_sphere's radius 1: one at the
// origin covers points 0.85 from it and misses those 1.1 away
bool testDefaultRadius(
    anari::Device d, anari::Frame frame, anari::Renderer renderer)
{
  const float3 center(0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
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

  anari::setParameter(d, renderer, "mode", "hitSurface");
  anari::commitParameters(d, renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  const float4 GREEN(0.f, 1.f, 0.f, 1.f);
  struct
  {
    float2 pos;
    bool hit;
  } probes[] = {{{0.85f, 0.f}, true},
      {{-0.6f, 0.6f}, true},
      {{0.8f, -0.8f}, false}};
  bool pass = true;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  for (const auto &p : probes) {
    const uint2 px = pixelAt(p.pos);
    const bool hit =
        fb.data && fb.data[size_t(px.y) * IMAGE_SIZE.x + px.x] == GREEN;
    if (hit != p.hit) {
      std::printf("FAIL default radius: (%g, %g) %s\n",
          p.pos.x,
          p.pos.y,
          p.hit ? "missed" : "hit");
      pass = false;
    }
  }
  anari::unmap(d, frame, "channel.color");
  anari::release(d, world);

  std::printf("%s default radius\n", pass ? "PASS" : "FAIL");
  return pass;
}

// device errors and warnings fail the test
bool g_deviceMessage = false;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING) {
    g_deviceMessage = true;
    std::fprintf(stderr, "[ANARI] %s\n", message);
  }
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
  anari::setParameter(d, renderer, "background", BACKGROUND);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  bool pass = true;
  for (const auto &test : MODE_TESTS)
    pass &= testMode(d, frame, renderer, test);
  pass &= testDefaultRadius(d, frame, renderer);

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceMessage)
    std::printf("FAIL: device reported warnings or errors\n");

  return pass && !g_deviceMessage ? 0 : 1;
}
