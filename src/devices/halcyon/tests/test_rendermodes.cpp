// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders one scene in every renderer 'mode' and checks probe pixels of the
// float color channel against values computed from the scene. Two instances
// share a quad: one scaled and rotated (so the world-space normal differs
// from both the object-space normal and the plain transformed one), one
// turned to face away from the camera.

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
const float3 RAY_DIR = {0.f, 0.f, -1.f};

// The quad: two triangles of half-width 0.3 in the object-space plane z = 0,
// facing +z; triangle 0 is the lower-right half (x > y), triangle 1 the
// upper-left
const std::vector<float3> VERTICES = {{-0.3f, -0.3f, 0.f},
    {0.3f, -0.3f, 0.f},
    {0.3f, 0.3f, 0.f},
    {-0.3f, 0.3f, 0.f}};
const std::vector<uint3> INDICES = {{0, 1, 2}, {0, 2, 3}};
const std::vector<uint32_t> PRIMITIVE_IDS = {100, 101};
const std::vector<float3> VERTEX_COLORS = {
    {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 0.f}};
// the second value exceeds 1, which shading clamps
const std::vector<float3> PRIMITIVE_ATTRIBUTE0 = {
    {0.1f, 0.2f, 0.3f}, {2.f, 0.5f, 0.25f}};
const float4 ATTRIBUTE1 = {0.4f, 0.5f, 0.6f, 1.f};
const uint32_t SURFACE_ID = 7;
const float MATERIAL_OPACITY = 0.5f;
const uint32_t DUMMY_SURFACE_ID = 9;

struct InstanceDesc
{
  mat4 xfm;
  uint32_t id;
  uint32_t embreeGeomID; // index of the quad's surface in the group
};

const float PI = 3.14159265f;

const std::vector<InstanceDesc> INSTANCES = {
    {mul(linalg::translation_matrix(float3(-0.5f, 0.f, 0.f)),
         mul(linalg::scaling_matrix(float3(2.f, 1.f, 1.f)),
             linalg::rotation_matrix(
                 linalg::rotation_quat(float3(0.f, 1.f, 0.f), PI / 4.f)))),
        20,
        0},
    {mul(linalg::translation_matrix(float3(0.5f, 0.f, 0.f)),
         linalg::rotation_matrix(
             linalg::rotation_quat(float3(0.f, 1.f, 0.f), PI))),
        21,
        1},
};

anari::Surface makeSurface(anari::Device d,
    const std::vector<float3> &verts,
    uint32_t id,
    bool withAttributes)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", verts.data(), verts.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", INDICES.data(), INDICES.size());
  if (withAttributes) {
    anari::setParameterArray1D(
        d, geom, "primitive.id", PRIMITIVE_IDS.data(), PRIMITIVE_IDS.size());
    anari::setParameterArray1D(
        d, geom, "vertex.color", VERTEX_COLORS.data(), VERTEX_COLORS.size());
    anari::setParameterArray1D(d,
        geom,
        "primitive.attribute0",
        PRIMITIVE_ATTRIBUTE0.data(),
        PRIMITIVE_ATTRIBUTE0.size());
    anari::setParameter(d, geom, "attribute1", ATTRIBUTE1);
  }
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", float3(0.8f));
  // only 'opacityHeatmap' and the default mode show opacity
  anari::setParameter(d, mat, "opacity", MATERIAL_OPACITY);
  anari::setParameter(d, mat, "alphaMode", "blend");
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

// Instance 0's group holds just the quad; instance 1's has an off-screen
// dummy surface first, so the quad is its Embree geometry 1. Instance 1 also
// sets 'attribute3', which the geometry.* modes ignore.
anari::World makeWorld(anari::Device d)
{
  auto quad = makeSurface(d, VERTICES, SURFACE_ID, true);
  std::vector<float3> farVerts;
  for (auto v : VERTICES)
    farVerts.push_back(v + float3(100.f, 0.f, 0.f));
  auto dummy = makeSurface(d, farVerts, DUMMY_SURFACE_ID, false);

  std::vector<anari::Instance> instances;
  for (size_t i = 0; i < INSTANCES.size(); i++) {
    std::vector<anari::Surface> surfaces;
    if (INSTANCES[i].embreeGeomID == 1)
      surfaces.push_back(dummy);
    surfaces.push_back(quad);

    auto group = anari::newObject<anari::Group>(d);
    anari::setParameterArray1D(
        d, group, "surface", surfaces.data(), surfaces.size());
    anari::commitParameters(d, group);

    auto inst = anari::newObject<anari::Instance>(d, "transform");
    anari::setAndReleaseParameter(d, inst, "group", group);
    anari::setParameter(d, inst, "transform", INSTANCES[i].xfm);
    anari::setParameter(d, inst, "id", INSTANCES[i].id);
    if (i == 1)
      anari::setParameter(d, inst, "attribute3", float4(1.f));
    anari::commitParameters(d, inst);
    instances.push_back(inst);
  }
  anari::release(d, quad);
  anari::release(d, dummy);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "instance", instances.data(), instances.size());
  anari::commitParameters(d, world);
  for (auto i : instances)
    anari::release(d, i);
  return world;
}

// Orthographic camera looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", RAY_DIR);
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// Hit of a probe pixel, computed from the scene rather than rendered
struct Hit
{
  const InstanceDesc *inst;
  uint32_t instIndex;
  uint32_t prim; // triangle index
  float u, v; // Embree barycentrics: p = (1-u-v) v0 + u v1 + v v2
  float3 worldNormal; // normalized
};

struct Probe
{
  const char *name;
  uint2 pixel;
  bool hit;
  Hit h;
};

// Pixel covering world-space 'pos' on the z = 0 plane
uint2 pixelAt(float2 pos)
{
  return uint2(uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x),
      uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y));
}

// Probe at the pixel showing object-space point 'p' of instance 'i', with the
// hit recomputed for the ray through that pixel's centre (where the camera
// samples it)
Probe makeHitProbe(const char *name, uint32_t i, float3 p)
{
  const InstanceDesc &inst = INSTANCES[i];
  const float4 world = mul(inst.xfm, float4(p, 1.f));
  const uint2 px = pixelAt(float2(world.x, world.y));
  const float3 org(2.f * (px.x + 0.5f) / IMAGE_SIZE.x - 1.f,
      2.f * (px.y + 0.5f) / IMAGE_SIZE.y - 1.f,
      2.f);

  const mat4 inv = linalg::inverse(inst.xfm);
  const float4 o = mul(inv, float4(org, 1.f));
  const float4 dir = mul(inv, float4(RAY_DIR, 0.f));
  const float t = -o.z / dir.z;
  const float2 q(o.x + t * dir.x, o.y + t * dir.y);

  Hit h;
  h.inst = &inst;
  h.instIndex = i;
  h.prim = q.x > q.y ? 0 : 1;
  const uint3 idx = INDICES[h.prim];
  const float2 v0(VERTICES[idx.x].x, VERTICES[idx.x].y);
  const float2 e1 = float2(VERTICES[idx.y].x, VERTICES[idx.y].y) - v0;
  const float2 e2 = float2(VERTICES[idx.z].x, VERTICES[idx.z].y) - v0;
  const float2 r = q - v0;
  const float det = e1.x * e2.y - e1.y * e2.x;
  h.u = (r.x * e2.y - r.y * e2.x) / det;
  h.v = (e1.x * r.y - e1.y * r.x) / det;

  // inverse transpose of the linear part applied to the object normal +z
  const mat3 m(float3(inst.xfm[0].x, inst.xfm[0].y, inst.xfm[0].z),
      float3(inst.xfm[1].x, inst.xfm[1].y, inst.xfm[1].z),
      float3(inst.xfm[2].x, inst.xfm[2].y, inst.xfm[2].z));
  h.worldNormal = linalg::normalize(
      mul(linalg::transpose(linalg::inverse(m)), float3(0.f, 0.f, 1.f)));

  return {name, px, true, h};
}

const std::vector<Probe> &probes()
{
  static const std::vector<Probe> retval = {
      {"miss", pixelAt(float2(0.f, 0.9f)), false, {}},
      makeHitProbe("instance 0, tri 0", 0, float3(0.15f, -0.1f, 0.f)),
      makeHitProbe("instance 0, tri 1", 0, float3(-0.15f, 0.1f, 0.f)),
      makeHitProbe("instance 1, tri 0", 1, float3(0.15f, -0.1f, 0.f)),
      makeHitProbe("instance 1, tri 1", 1, float3(-0.1f, 0.15f, 0.f)),
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

float3 boolColor(bool pred)
{
  return pred ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
}

float3 vertexColor(const Hit &h)
{
  const uint3 idx = INDICES[h.prim];
  return (1.f - h.u - h.v) * VERTEX_COLORS[idx.x] + h.u * VERTEX_COLORS[idx.y]
      + h.v * VERTEX_COLORS[idx.z];
}

// Expected surface color of a hit in a mode, before clamping to 1
using Expected = std::function<float3(const Hit &)>;

struct ModeTest
{
  const char *mode;
  Expected expected;
};

const std::vector<ModeTest> MODE_TESTS = {
    {"primitiveId",
        [](const Hit &h) { return randomColor(PRIMITIVE_IDS[h.prim]); }},
    {"objectId", [](const Hit &) { return randomColor(SURFACE_ID); }},
    {"instanceId", [](const Hit &h) { return randomColor(h.inst->id); }},
    {"embreePrimID", [](const Hit &h) { return randomColor(h.prim); }},
    {"embreeGeomID",
        [](const Hit &h) { return randomColor(h.inst->embreeGeomID); }},
    {"embreeInstID", [](const Hit &h) { return randomColor(h.instIndex); }},
    {"Ng", [](const Hit &h) { return h.worldNormal; }},
    {"Ng.abs",
        [](const Hit &h) {
          return linalg::normalize(linalg::abs(h.worldNormal));
        }},
    // no normals: blue, Ns is Ng
    {"Ns.flipped", [](const Hit &) { return float3(0.f, 0.f, 1.f); }},
    {"uvw", [](const Hit &h) { return float3(h.u, h.v, 1.f); }},
    {"backface",
        [](const Hit &h) {
          return boolColor(linalg::dot(h.worldNormal, RAY_DIR) < 0.f);
        }},
    {"hitSurface", [](const Hit &) { return boolColor(true); }},
    {"geometry.attribute0",
        [](const Hit &h) { return PRIMITIVE_ATTRIBUTE0[h.prim]; }},
    {"geometry.attribute1",
        [](const Hit &) {
          return float3(ATTRIBUTE1.x, ATTRIBUTE1.y, ATTRIBUTE1.z);
        }},
    {"geometry.attribute2", [](const Hit &) { return float3(0.f); }},
    // the instance's 'attribute3' doesn't count
    {"geometry.attribute3", [](const Hit &) { return float3(0.f); }},
    {"geometry.color", vertexColor},
    // helide's heatmap (blue, red, yellow, white) at opacity 0.5: halfway
    // from red to yellow
    {"opacityHeatmap",
        [](const Hit &h) {
          const float falloff = std::abs(linalg::dot(-RAY_DIR, h.worldNormal));
          return float3(1.f, 0.5f, 0.f) * (0.8f * falloff + 0.2f);
        }},
    // unknown modes shade like "default": 0.8 gray, half eye-lit, blended
    // over the background by the material's opacity (the only mode here that
    // composites; the pixel's alpha stays 1 over the opaque background)
    {"no such mode",
        [](const Hit &h) {
          const float falloff = std::abs(linalg::dot(-RAY_DIR, h.worldNormal));
          const float3 surface(0.8f * (0.5f * falloff + 0.5f));
          return MATERIAL_OPACITY * surface
              + (1.f - MATERIAL_OPACITY)
              * float3(BACKGROUND.x, BACKGROUND.y, BACKGROUND.z);
        }},
};

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
}

bool checkPixel(const char *mode,
    const char *what,
    const std::vector<float4> &pixels,
    uint2 px,
    float4 expected)
{
  const float4 got = pixels[size_t(px.y) * IMAGE_SIZE.x + px.x];
  if (near(got, expected))
    return true;
  std::printf("FAIL %s, %s: got (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
      mode,
      what,
      got.x,
      got.y,
      got.z,
      got.w,
      expected.x,
      expected.y,
      expected.z,
      expected.w);
  return false;
}

// Sets 'mode' on 'renderer', renders and returns the float color channel
// (and the depth channel in 'depth', if given)
std::vector<float4> render(anari::Device d,
    anari::Frame frame,
    anari::Renderer renderer,
    const char *mode,
    std::vector<float> *depth = nullptr)
{
  anari::setParameter(d, renderer, "mode", mode);
  anari::commitParameters(d, renderer);
  anari::render(d, frame);
  anari::wait(d, frame);

  std::vector<float4> pixels;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  else
    std::printf("FAIL %s: could not map channel.color\n", mode);
  anari::unmap(d, frame, "channel.color");

  if (depth) {
    auto db = anari::map<float>(d, frame, "channel.depth");
    if (db.data && db.width == IMAGE_SIZE.x && db.height == IMAGE_SIZE.y)
      depth->assign(db.data, db.data + db.width * db.height);
    else
      std::printf("FAIL %s: could not map channel.depth\n", mode);
    anari::unmap(d, frame, "channel.depth");
  }
  return pixels;
}

bool testMode(anari::Device d,
    anari::Frame frame,
    anari::Renderer renderer,
    const ModeTest &test)
{
  const auto pixels = render(d, frame, renderer, test.mode);
  if (pixels.empty())
    return false;

  bool pass = true;
  for (const auto &p : probes()) {
    const float4 expected = p.hit
        ? float4(linalg::min(test.expected(p.h), float3(1.f)), 1.f)
        : BACKGROUND;
    pass &= checkPixel(test.mode, p.name, pixels, p.pixel, expected);
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test.mode);
  return pass;
}

// Every pixel is the ray direction mapped to [0, 1] at depth 0, hit or not
bool testTestFrame(
    anari::Device d, anari::Frame frame, anari::Renderer renderer)
{
  std::vector<float> depth;
  const auto pixels = render(d, frame, renderer, "testFrame", &depth);
  if (pixels.empty() || depth.empty())
    return false;

  const float4 expected(RAY_DIR * 0.5f + 0.5f, 1.f);
  bool pass = true;
  for (const auto &p : probes()) {
    pass &= checkPixel("testFrame", p.name, pixels, p.pixel, expected);
    const float z = depth[size_t(p.pixel.y) * IMAGE_SIZE.x + p.pixel.x];
    if (z != 0.f) {
      std::printf("FAIL testFrame, %s: depth %f, expected 0\n", p.name, z);
      pass = false;
    }
  }
  std::printf("%s testFrame\n", pass ? "PASS" : "FAIL");
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

  // Modes switch by recommitting 'renderer'
  auto world = makeWorld(d);
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  // The expected colours are those of the eye light alone, without ambient
  // occlusion (on by default)
  anari::setParameter(d, renderer, "ambientOcclusion", false);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
  anari::setParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  bool pass = true;
  for (const auto &test : MODE_TESTS)
    pass &= testMode(d, frame, renderer, test);
  pass &= testTestFrame(d, frame, renderer);

  anari::release(d, frame);
  anari::release(d, renderer);
  anari::release(d, world);
  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
