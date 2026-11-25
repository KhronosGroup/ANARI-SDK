// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders cylinder and cone geometry through the device and checks probe
// pixels of the float color channel in debug modes: vertex colors (linear
// along each primitive), primitive IDs, Embree primitive indices and
// geometric normals. Four surfaces:
//   - indexed cylinders with per-primitive radii and 'primitive.id', and a
//     vertex no primitive uses
//   - implicitly indexed cylinders with the global 'radius'
//   - indexed cones with per-vertex radii
//   - implicitly indexed cones with the global 'radius'
// All primitives are horizontal, so the hit's u is the ray's x along the
// primitive.

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

// Indexed cylinders 3 -> 4 and 0 -> 1; vertex 2 is unused (between the strip's
// cylinders, where nothing is drawn)
const std::vector<float3> CYLINDER_VERTICES = {{-0.8f, 0.75f, 0.f},
    {0.8f, 0.75f, 0.f},
    {0.f, 0.f, 0.f},
    {-0.8f, 0.4f, 0.f},
    {0.8f, 0.4f, 0.f}};
const std::vector<float3> CYLINDER_COLORS = {{1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f},
    {1.f, 1.f, 0.f},
    {0.f, 1.f, 1.f}};
const std::vector<uint2> CYLINDER_INDICES = {{3, 4}, {0, 1}};
const std::vector<float> CYLINDER_RADII = {0.15f, 0.08f};
const std::vector<uint32_t> PRIMITIVE_IDS = {100, 101};

// Implicitly indexed cylinders 0 -> 1 and 2 -> 3, radius STRIP_RADIUS
const std::vector<float3> STRIP_VERTICES = {{-0.8f, 0.05f, 0.f},
    {-0.1f, 0.05f, 0.f},
    {0.1f, 0.05f, 0.f},
    {0.8f, 0.05f, 0.f}};
const std::vector<float3> STRIP_COLORS = {
    {1.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f}};
const float STRIP_RADIUS = 0.1f;

// Indexed cones 1 -> 0 (narrowing to the right) and 2 -> 3
const std::vector<float3> CONE_VERTICES = {{0.8f, -0.35f, 0.f},
    {-0.8f, -0.35f, 0.f},
    {-0.8f, -0.8f, 0.f},
    {-0.1f, -0.8f, 0.f}};
const std::vector<float> CONE_RADII = {0.05f, 0.2f, 0.05f, 0.15f};
const std::vector<float3> CONE_COLORS = {
    {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, 1.f}};
const std::vector<uint2> CONE_INDICES = {{1, 0}, {2, 3}};

// Implicitly indexed cone 0 -> 1 with the global 'radius'
const std::vector<float3> GLOBAL_CONE_VERTICES = {
    {0.1f, -0.8f, 0.f}, {0.8f, -0.8f, 0.f}};
const std::vector<float3> GLOBAL_CONE_COLORS = {
    {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}};
const float GLOBAL_CONE_RADIUS = 0.12f;

// A primitive of one of the surfaces, p0 -> p1 along +x
struct PrimitiveRef
{
  float3 p0, p1;
  float r0, r1; // radius at p0 and p1
  float3 c0, c1; // vertex colors at p0 and p1
  uint32_t prim; // Embree primID
  uint32_t primitiveId; // 'primitive.id', or 'prim' without one
};

PrimitiveRef cylinder(uint32_t i)
{
  const uint2 idx = CYLINDER_INDICES[i];
  return {CYLINDER_VERTICES[idx.x],
      CYLINDER_VERTICES[idx.y],
      CYLINDER_RADII[i],
      CYLINDER_RADII[i],
      CYLINDER_COLORS[idx.x],
      CYLINDER_COLORS[idx.y],
      i,
      PRIMITIVE_IDS[i]};
}

PrimitiveRef stripCylinder(uint32_t i)
{
  return {STRIP_VERTICES[2 * i],
      STRIP_VERTICES[2 * i + 1],
      STRIP_RADIUS,
      STRIP_RADIUS,
      STRIP_COLORS[2 * i],
      STRIP_COLORS[2 * i + 1],
      i,
      i};
}

PrimitiveRef cone(uint32_t i)
{
  const uint2 idx = CONE_INDICES[i];
  return {CONE_VERTICES[idx.x],
      CONE_VERTICES[idx.y],
      CONE_RADII[idx.x],
      CONE_RADII[idx.y],
      CONE_COLORS[idx.x],
      CONE_COLORS[idx.y],
      i,
      i};
}

PrimitiveRef globalCone()
{
  return {GLOBAL_CONE_VERTICES[0],
      GLOBAL_CONE_VERTICES[1],
      GLOBAL_CONE_RADIUS,
      GLOBAL_CONE_RADIUS,
      GLOBAL_CONE_COLORS[0],
      GLOBAL_CONE_COLORS[1],
      0,
      0};
}

template <typename T>
void setArray(anari::Device d,
    anari::Geometry geom,
    const char *name,
    const std::vector<T> &v)
{
  anari::setParameterArray1D(d, geom, name, v.data(), v.size());
}

anari::Surface makeSurface(anari::Device d, anari::Geometry geom)
{
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
  auto cylinders = anari::newObject<anari::Geometry>(d, "cylinder");
  setArray(d, cylinders, "vertex.position", CYLINDER_VERTICES);
  setArray(d, cylinders, "vertex.color", CYLINDER_COLORS);
  setArray(d, cylinders, "primitive.index", CYLINDER_INDICES);
  setArray(d, cylinders, "primitive.radius", CYLINDER_RADII);
  setArray(d, cylinders, "primitive.id", PRIMITIVE_IDS);

  auto strip = anari::newObject<anari::Geometry>(d, "cylinder");
  setArray(d, strip, "vertex.position", STRIP_VERTICES);
  setArray(d, strip, "vertex.color", STRIP_COLORS);
  anari::setParameter(d, strip, "radius", STRIP_RADIUS);

  auto cones = anari::newObject<anari::Geometry>(d, "cone");
  setArray(d, cones, "vertex.position", CONE_VERTICES);
  setArray(d, cones, "vertex.color", CONE_COLORS);
  setArray(d, cones, "vertex.radius", CONE_RADII);
  setArray(d, cones, "primitive.index", CONE_INDICES);

  auto globalCones = anari::newObject<anari::Geometry>(d, "cone");
  setArray(d, globalCones, "vertex.position", GLOBAL_CONE_VERTICES);
  setArray(d, globalCones, "vertex.color", GLOBAL_CONE_COLORS);
  anari::setParameter(d, globalCones, "radius", GLOBAL_CONE_RADIUS);

  anari::Surface surfaces[] = {makeSurface(d, cylinders),
      makeSurface(d, strip),
      makeSurface(d, cones),
      makeSurface(d, globalCones)};
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", surfaces, 4);
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
  PrimitiveRef prim;
  float3 color; // vertex color interpolated at the pixel's ray
  float3 normal; // unit surface normal at the pixel's ray
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

// Probe at the pixel covering the point 't' along primitive 'p', 'dy' off its
// axis; 't' has to be away from the ends, which are open
Probe makeHitProbe(const char *name, const PrimitiveRef &p, float t, float dy)
{
  const float3 a = p.p0 + t * (p.p1 - p.p0);
  const uint2 px = pixelAt(float2(a.x, a.y + dy));
  const float2 ray = rayAt(px);
  const float length = p.p1.x - p.p0.x;
  const float u = (ray.x - p.p0.x) / length;
  const float r = p.r0 + u * (p.r1 - p.r0);
  const float d = ray.y - p.p0.y;
  // the radial direction, tilted back along the axis by the cone's slope
  const float slope = (p.r1 - p.r0) / length;
  const float3 n =
      normalize(float3(-slope, d / r, std::sqrt(r * r - d * d) / r));
  return {name, px, true, p, p.c0 + u * (p.c1 - p.c0), n};
}

Probe makeMissProbe(const char *name, float2 pos)
{
  return {name, pixelAt(pos), false, {}, {}, {}};
}

const std::vector<Probe> &probes()
{
  static const std::vector<Probe> retval = {
      makeMissProbe("miss", float2(0.f, 0.95f)),
      makeHitProbe("cylinder 0", cylinder(0), 0.3f, 0.f),
      makeHitProbe("cylinder 0, far side", cylinder(0), 0.8f, -0.1f),
      makeHitProbe("cylinder 1", cylinder(1), 0.6f, 0.04f),
      // 0.14 above cylinder 1: outside its radius, inside cylinder 0's
      makeMissProbe(
          "cylinder 1, outside primitive radius", float2(-0.3f, 0.92f)),
      makeHitProbe("strip 0", stripCylinder(0), 0.5f, 0.05f),
      makeHitProbe("strip 1", stripCylinder(1), 0.4f, -0.05f),
      makeMissProbe("strip, between primitives", float2(0.f, 0.05f)),
      makeHitProbe("cone 0, wide end", cone(0), 0.2f, 0.1f),
      makeHitProbe("cone 0, narrow end", cone(0), 0.85f, 0.f),
      makeMissProbe("cone 0, outside narrow end", float2(0.5f, -0.35f + 0.12f)),
      makeHitProbe("cone 1", cone(1), 0.7f, -0.08f),
      makeMissProbe("cone 1, outside narrow end", float2(-0.65f, -0.66f)),
      makeHitProbe("global radius cone", globalCone(), 0.5f, 0.07f),
      makeMissProbe("global radius cone, outside", float2(0.45f, -0.95f)),
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
        [](const Probe &p) { return randomColor(p.prim.primitiveId); },
        1e-4f},
    {"embreePrimID",
        [](const Probe &p) { return randomColor(p.prim.prim); },
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

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceMessage)
    std::printf("FAIL: device reported warnings or errors\n");

  return pass && !g_deviceMessage ? 0 : 1;
}
