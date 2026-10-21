// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders curve geometry through the device and checks probe pixels of the
// float color channel in debug modes: vertex colors (linear along each
// segment), primitive IDs, Embree primitive indices and geometric normals.
// One surface has indexed segments with per-vertex radii and a vertex (2)
// no segment reaches; the other is implicitly indexed with the global
// 'radius'. All segments are horizontal, so the hit's u is the ray's x along
// the segment. The modes run twice, with UINT32 and UINT64 'primitive.index'.

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

// Indexed: segments 3 -> 4 and 0 -> 1; nothing starts at 1 or 2, so
// 1 -> 2 isn't drawn
const std::vector<float3> INDEXED_VERTICES = {{-0.8f, 0.5f, 0.f},
    {0.f, 0.5f, 0.f},
    {0.8f, 0.5f, 0.f},
    {-0.8f, 0.f, 0.f},
    {0.8f, 0.f, 0.f}};
const std::vector<float> INDEXED_RADII = {0.1f, 0.1f, 0.1f, 0.2f, 0.2f};
const std::vector<float3> INDEXED_COLORS = {{1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f},
    {1.f, 1.f, 0.f},
    {0.f, 1.f, 1.f}};
const std::vector<uint32_t> INDICES = {3, 0};
const std::vector<uint32_t> PRIMITIVE_IDS = {100, 101};

// Strip: consecutive vertices, all of radius STRIP_RADIUS
const std::vector<float3> STRIP_VERTICES = {
    {-0.8f, -0.6f, 0.f}, {0.f, -0.6f, 0.f}, {0.8f, -0.6f, 0.f}};
const float STRIP_RADIUS = 0.1f;
const std::vector<float3> STRIP_COLORS = {
    {1.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 1.f}};

// A segment of one of the surfaces
struct SegmentRef
{
  float3 p0, p1;
  float radius;
  float3 c0, c1; // vertex colors at p0 and p1
  uint32_t prim; // Embree primID
  uint32_t primitiveId; // 'primitive.id', or 'prim' without one
};

SegmentRef indexedSegment(uint32_t i)
{
  const uint32_t v = INDICES[i];
  return {INDEXED_VERTICES[v],
      INDEXED_VERTICES[v + 1],
      INDEXED_RADII[v],
      INDEXED_COLORS[v],
      INDEXED_COLORS[v + 1],
      i,
      PRIMITIVE_IDS[i]};
}

SegmentRef stripSegment(uint32_t i)
{
  return {STRIP_VERTICES[i],
      STRIP_VERTICES[i + 1],
      STRIP_RADIUS,
      STRIP_COLORS[i],
      STRIP_COLORS[i + 1],
      i,
      i};
}

anari::Surface makeSurface(anari::Device d, bool indexed, bool uint64Indices)
{
  auto geom = anari::newObject<anari::Geometry>(d, "curve");
  const auto &verts = indexed ? INDEXED_VERTICES : STRIP_VERTICES;
  const auto &colors = indexed ? INDEXED_COLORS : STRIP_COLORS;
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "vertex.color", colors.data(), colors.size());
  if (indexed) {
    anari::setParameterArray1D(
        d, geom, "vertex.radius", INDEXED_RADII.data(), INDEXED_RADII.size());
    if (uint64Indices) {
      const std::vector<uint64_t> indices(INDICES.begin(), INDICES.end());
      anari::setParameterArray1D(
          d, geom, "primitive.index", indices.data(), indices.size());
    } else {
      anari::setParameterArray1D(
          d, geom, "primitive.index", INDICES.data(), INDICES.size());
    }
    anari::setParameterArray1D(
        d, geom, "primitive.id", PRIMITIVE_IDS.data(), PRIMITIVE_IDS.size());
  } else {
    anari::setParameter(d, geom, "radius", STRIP_RADIUS);
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

anari::World makeWorld(anari::Device d, bool uint64Indices)
{
  anari::Surface surfaces[] = {makeSurface(d, true, uint64Indices),
      makeSurface(d, false, uint64Indices)};
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
  SegmentRef segment;
  float3 color; // vertex color interpolated at the pixel's ray
  float3 normal; // unit normal of the tube at the pixel's ray
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

// Probe at the pixel covering the point 't' along segment 's', 'dy' off its
// axis; 't' has to be away from the ends, where the round caps are
Probe makeHitProbe(const char *name, const SegmentRef &s, float t, float dy)
{
  const float3 p = s.p0 + t * (s.p1 - s.p0);
  const uint2 px = pixelAt(float2(p.x, p.y + dy));
  const float2 ray = rayAt(px);
  const float u = (ray.x - s.p0.x) / (s.p1.x - s.p0.x);
  const float d = ray.y - s.p0.y;
  const float3 n =
      float3(0.f, d, std::sqrt(s.radius * s.radius - d * d)) / s.radius;
  return {name, px, true, s, s.c0 + u * (s.c1 - s.c0), n};
}

Probe makeMissProbe(const char *name, float2 pos)
{
  return {name, pixelAt(pos), false, {}, {}, {}};
}

const std::vector<Probe> &probes()
{
  static const std::vector<Probe> retval = {
      makeMissProbe("miss", float2(0.f, 0.9f)),
      makeMissProbe("no segment from vertex 1", float2(0.4f, 0.5f)),
      makeHitProbe("indexed 0", indexedSegment(0), 0.3f, 0.f),
      makeHitProbe("indexed 0, far side", indexedSegment(0), 0.8f, -0.05f),
      // inside vertex 3's radius 0.2, outside vertex 0's 0.1
      makeHitProbe("indexed 0, vertex radius", indexedSegment(0), 0.5f, 0.15f),
      makeHitProbe("indexed 1", indexedSegment(1), 0.4f, 0.03f),
      makeMissProbe("indexed 1, outside vertex radius", float2(-0.4f, 0.65f)),
      makeHitProbe("strip 0", stripSegment(0), 0.6f, 0.f),
      makeHitProbe("strip 1, global radius", stripSegment(1), 0.3f, -0.05f),
      makeMissProbe("strip 1, outside global radius", float2(0.4f, -0.75f)),
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
    // Embree's u on a tube is close to, not exactly, the axis parameter
    {"geometry.color", [](const Probe &p) { return p.color; }, 1e-3f},
    {"primitiveId",
        [](const Probe &p) { return randomColor(p.segment.primitiveId); },
        1e-4f},
    {"embreePrimID",
        [](const Probe &p) { return randomColor(p.segment.prim); },
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
    const ModeTest &test,
    const char *label)
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
            "FAIL %s %s, %s: got (%f, %f, %f, %f), expected (%f, %f, "
            "%f, %f)\n",
            label,
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

  std::printf("%s %s %s\n", pass ? "PASS" : "FAIL", label, test.mode);
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

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
  anari::setParameter(d, frame, "renderer", renderer);

  bool pass = true;
  for (const bool uint64Indices : {false, true}) {
    anari::setAndReleaseParameter(
        d, frame, "world", makeWorld(d, uint64Indices));
    anari::commitParameters(d, frame);
    for (const auto &test : MODE_TESTS) {
      pass &= testMode(
          d, frame, renderer, test, uint64Indices ? "UINT64" : "UINT32");
    }
  }

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceMessage)
    std::printf("FAIL: device reported warnings or errors\n");

  return pass && !g_deviceMessage ? 0 : 1;
}
