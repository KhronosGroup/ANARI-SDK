// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders quad geometry through the device and checks probe pixels of the
// float color channel in the debug modes that show quad hits: vertex colors
// (bilinear), primitive IDs and Embree primitive indices. One surface has
// indexed quads whose first vertex isn't the lower-left corner; the other is
// a quad soup (implicit indices).

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};

// Indexed: two quads side by side in x = [-0.9, -0.1], y = [-0.4, 0.4]
//   3---4---5
//   |q0 |q1 |
//   0---1---2
// q0 starts at the lower-left corner; q1 starts at its lower-right corner and
// runs counterclockwise, so its u runs along +y and v along -x
const std::vector<float3> INDEXED_VERTICES = {{-0.9f, -0.4f, 0.f},
    {-0.5f, -0.4f, 0.f},
    {-0.1f, -0.4f, 0.f},
    {-0.9f, 0.4f, 0.f},
    {-0.5f, 0.4f, 0.f},
    {-0.1f, 0.4f, 0.f}};
const std::vector<uint4> INDICES = {{0, 1, 4, 3}, {2, 5, 4, 1}};
const std::vector<uint32_t> PRIMITIVE_IDS = {100, 101};
const std::vector<float3> INDEXED_COLORS = {{1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f},
    {1.f, 1.f, 0.f},
    {0.f, 1.f, 1.f},
    {1.f, 0.f, 1.f}};

// Soup: two quads in x = [0.1, 0.9], y = [-0.4, 0.4], each counterclockwise
// from its lower-left corner
const std::vector<float3> SOUP_VERTICES = {{0.1f, -0.4f, 0.f},
    {0.5f, -0.4f, 0.f},
    {0.5f, 0.4f, 0.f},
    {0.1f, 0.4f, 0.f},
    {0.5f, -0.4f, 0.f},
    {0.9f, -0.4f, 0.f},
    {0.9f, 0.4f, 0.f},
    {0.5f, 0.4f, 0.f}};
const std::vector<float3> SOUP_COLORS = {{1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f},
    {1.f, 1.f, 1.f},
    {0.5f, 0.f, 0.f},
    {0.f, 0.5f, 0.f},
    {0.f, 0.f, 0.5f},
    {0.5f, 0.5f, 0.5f}};

// A quad of one of the surfaces
struct QuadRef
{
  const std::vector<float3> *vertices;
  const std::vector<float3> *colors;
  uint4 idx;
  uint32_t prim; // Embree primID
  uint32_t primitiveId; // 'primitive.id', or 'prim' without one
};

QuadRef indexedQuad(uint32_t i)
{
  return {&INDEXED_VERTICES, &INDEXED_COLORS, INDICES[i], i, PRIMITIVE_IDS[i]};
}

QuadRef soupQuad(uint32_t i)
{
  return {&SOUP_VERTICES,
      &SOUP_COLORS,
      uint4(4 * i, 4 * i + 1, 4 * i + 2, 4 * i + 3),
      i,
      i};
}

anari::Surface makeSurface(anari::Device d, bool indexed)
{
  auto geom = anari::newObject<anari::Geometry>(d, "quad");
  const auto &verts = indexed ? INDEXED_VERTICES : SOUP_VERTICES;
  const auto &colors = indexed ? INDEXED_COLORS : SOUP_COLORS;
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "vertex.color", colors.data(), colors.size());
  if (indexed) {
    anari::setParameterArray1D(
        d, geom, "primitive.index", INDICES.data(), INDICES.size());
    anari::setParameterArray1D(
        d, geom, "primitive.id", PRIMITIVE_IDS.data(), PRIMITIVE_IDS.size());
  }
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
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
  QuadRef quad;
  float3 color; // bilinear vertex color at the pixel's ray
};

// Probe at the pixel covering 'pos' on quad 'q'. The camera samples pixel
// centres, so the color is interpolated at the centre, in the quad's
// (u, v): a at (0, 0), b at (1, 0), c at (1, 1), d at (0, 1).
Probe makeHitProbe(const char *name, const QuadRef &q, float2 pos)
{
  const uint2 px(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
  const float2 p(2.f * (px.x + 0.5f) / IMAGE_SIZE.x - 1.f,
      2.f * (px.y + 0.5f) / IMAGE_SIZE.y - 1.f);

  const auto &v = *q.vertices;
  const float2 a(v[q.idx.x].x, v[q.idx.x].y);
  const float2 eu = float2(v[q.idx.y].x, v[q.idx.y].y) - a;
  const float2 ev = float2(v[q.idx.w].x, v[q.idx.w].y) - a;
  // the quads are rectangles, so (u, v) are projections onto the edges
  const float u = linalg::dot(p - a, eu) / linalg::dot(eu, eu);
  const float w = linalg::dot(p - a, ev) / linalg::dot(ev, ev);

  const auto &c = *q.colors;
  const float3 color = (1.f - w) * (1.f - u) * c[q.idx.x]
      + (1.f - w) * u * c[q.idx.y] + w * u * c[q.idx.z]
      + w * (1.f - u) * c[q.idx.w];
  return {name, px, true, q, color};
}

const std::vector<Probe> &probes()
{
  static const std::vector<Probe> retval = {
      {"miss", uint2(18, 34), false, {}, {}},
      makeHitProbe("indexed q0", indexedQuad(0), float2(-0.8f, -0.2f)),
      makeHitProbe(
          "indexed q0, upper right", indexedQuad(0), float2(-0.6f, 0.3f)),
      makeHitProbe("indexed q1", indexedQuad(1), float2(-0.2f, -0.3f)),
      makeHitProbe(
          "indexed q1, upper left", indexedQuad(1), float2(-0.4f, 0.2f)),
      makeHitProbe("soup q0", soupQuad(0), float2(0.2f, 0.25f)),
      makeHitProbe("soup q1", soupQuad(1), float2(0.8f, -0.25f)),
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
};

const std::vector<ModeTest> MODE_TESTS = {
    {"geometry.color", [](const Probe &p) { return p.color; }},
    {"primitiveId",
        [](const Probe &p) { return randomColor(p.quad.primitiveId); }},
    {"embreePrimID", [](const Probe &p) { return randomColor(p.quad.prim); }},
};

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
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
      if (!near(got, expected)) {
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

// device errors and warnings (e.g. a rejected 'primitive.index') fail the test
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
