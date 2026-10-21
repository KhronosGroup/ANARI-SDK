// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality''s random-walk subsurface scattering (Typhoon's RandomWalkSSS();
// renderer/pathtracer/Subsurface.isph), on openPBR materials of pure subsurface
// (subsurface_weight 1, specular_weight 0):
// - a colour of 0 absorbs everything: the surface renders black, exactly;
// - a white medium loses (almost) nothing: a white sphere under the ambient
//   light renders to its radiance (furnace test);
// - Chiang's remap makes the colour about the diffuse reflectance of a
//   semi-infinite slab: a deep box with a small radius, under the ambient
//   light, renders to a little less than colour times radiance;
// - entry, walk and exit are one bounce: with 'maxBounces' 0 the walk isn't
//   taken (black), with 1 the exit's direct light shows, and a lone convex
//   sphere renders the same image at 1 and 2;
// - the walk sees only its own surface: an opaque sphere inside, in the same
//   group or another instance, changes nothing (bitwise), nor do triangle
//   and quad meshes inside a box (in its group or another instance), a plate
//   its first walk rays reach, another transform of its instance array, or a
//   floor (a cut-out) in the plane of its bottom face, or a dense
//   transferFunction1D volume inside a box (walks don't track volumes);
// - radii are world units: a sphere scaled by an instance renders as the
//   same sphere built at that size, and a scene scaled down by 1e3 (radius
//   with it) renders as the original (the ray offsets follow the scale).

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {32, 32};
const float4 BACKGROUND = {0.f, 0.f, 0.f, 1.f};

int g_errors = 0;
int g_warnings = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR) {
    std::fprintf(stderr, "[ANARI ERROR] %s\n", message);
    g_errors++;
  } else if (severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI WARNING] %s\n", message);
    g_warnings++;
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

std::string str(float3 v)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", v.x, v.y, v.z);
  return buf;
}

// Scene //////////////////////////////////////////////////////////////////////

// An openPBR material of subsurface only: 'color', mean free path 'radius'
// (subsurface_radius, with a white radius scale), no specular layer
anari::Material makeSss(anari::Device d, float3 color, float radius)
{
  auto mat = anari::newObject<anari::Material>(d, "openPBR");
  anari::setParameter(d, mat, "subsurface_weight", 1.f);
  anari::setParameter(d, mat, "subsurface_color", color);
  anari::setParameter(d, mat, "subsurface_radius", radius);
  anari::setParameter(d, mat, "subsurface_radius_scale", float3(1.f));
  anari::setParameter(d, mat, "specular_weight", 0.f);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Material makeMatte(anari::Device d, float3 color)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geom, anari::Material mat)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Geometry makeSphereGeometry(anari::Device d, float3 center, float r)
{
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", r);
  anari::commitParameters(d, geom);
  return geom;
}

// The box [lo, hi] of triangles, faces outward
anari::Geometry makeBoxGeometry(anari::Device d, float3 lo, float3 hi)
{
  std::vector<float3> v;
  for (int i = 0; i < 8; i++) {
    v.push_back(
        float3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
  }
  const std::vector<uint3> indices = {{0, 2, 3},
      {0, 3, 1},
      {4, 5, 7},
      {4, 7, 6},
      {0, 1, 5},
      {0, 5, 4},
      {2, 6, 7},
      {2, 7, 3},
      {0, 4, 6},
      {0, 6, 2},
      {1, 3, 7},
      {1, 7, 5}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

// The box [lo, hi] of quads, faces outward
anari::Geometry makeQuadBoxGeometry(anari::Device d, float3 lo, float3 hi)
{
  std::vector<float3> v;
  for (int i = 0; i < 8; i++) {
    v.push_back(
        float3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
  }
  const std::vector<uint4> indices = {{0, 2, 3, 1},
      {4, 5, 7, 6},
      {0, 1, 5, 4},
      {2, 6, 7, 3},
      {0, 4, 6, 2},
      {1, 3, 7, 5}};
  auto geom = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

// The plane z = 'z' over [lo, hi] in x and y, of two triangles facing +z
anari::Geometry makePlateGeometry(
    anari::Device d, float2 lo, float2 hi, float z)
{
  const std::vector<float3> v = {
      {lo.x, lo.y, z}, {hi.x, lo.y, z}, {hi.x, hi.y, z}, {lo.x, hi.y, z}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

anari::Group makeGroup(anari::Device d, std::vector<anari::Surface> surfaces)
{
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(
      d, group, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, group);
  for (auto s : surfaces)
    anari::release(d, s);
  return group;
}

anari::Instance makeInstance(anari::Device d, anari::Group group, mat4 xfm)
{
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameter(d, inst, "transform", xfm);
  anari::commitParameters(d, inst);
  return inst;
}

// An instance array: 'group' at each of 'xfms'
anari::Instance makeInstanceArray(
    anari::Device d, anari::Group group, const std::vector<mat4> &xfms)
{
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
  anari::commitParameters(d, inst);
  return inst;
}

// A world of 'surfaces', 'instances' and 'lights' (all released)
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Instance> instances = {},
    std::vector<anari::Light> lights = {})
{
  auto world = anari::newObject<anari::World>(d);
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
  if (!instances.empty()) {
    anari::setParameterArray1D(
        d, world, "instance", instances.data(), instances.size());
  }
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto i : instances)
    anari::release(d, i);
  for (auto l : lights)
    anari::release(d, l);
  return world;
}

anari::Light makeDirectional(anari::Device d, float3 direction)
{
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", 2.f);
  anari::commitParameters(d, light);
  return light;
}

// A group of one dense transferFunction1D volume over the box [lo, hi]: a
// white, nearly opaque constant field (released into the group)
anari::Group makeVolumeGroup(anari::Device d, float3 lo, float3 hi)
{
  const float voxels[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels, 2, 2, 2);
  anari::setParameter(d, field, "origin", lo);
  anari::setParameter(d, field, "spacing", hi - lo);
  anari::commitParameters(d, field);
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", float3(1.f));
  anari::setParameter(d, vol, "opacity", 0.9f);
  anari::setParameter(d, vol, "unitDistance", 0.05f);
  anari::commitParameters(d, vol);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "volume", &vol, 1);
  anari::commitParameters(d, group);
  anari::release(d, vol);
  return group;
}

mat4 scaleTranslate(float s, float3 t)
{
  mat4 m = identity;
  m[0][0] = s;
  m[1][1] = s;
  m[2][2] = s;
  m[3] = float4(t, 1.f);
  return m;
}

struct Settings
{
  int pixelSamples{64};
  int maxBounces{16};
  // the ambient light's radiance (white), 0 for none
  float ambient{1.f};
  int seed{-1};
  // the orthographic camera, looking down -z, and its view height
  float3 position{0.f, 0.f, 3.f};
  float height{2.2f};
};

// The colour channel of 'world' through the pathtracer
std::vector<float4> render(
    anari::Device d, anari::World world, const Settings &s = {})
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "ambientColor", float3(1.f));
  anari::setParameter(d, renderer, "ambientRadiance", s.ambient);
  anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", s.maxBounces);
  anari::setParameter(d, renderer, "randomNumberSeed", s.seed);
  anari::setParameter(d, renderer, "fireflyClampThreshold", 0.f);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", s.position);
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", s.height);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  std::vector<float4> pixels;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

// Whether pixel (x, y) sees a disc of the image's 'fraction' of its radius
// around the center (well inside a sphere filling the view)
bool inDisc(uint32_t x, uint32_t y, float fraction)
{
  const float cx = (x + 0.5f) / IMAGE_SIZE.x - 0.5f;
  const float cy = (y + 0.5f) / IMAGE_SIZE.y - 0.5f;
  return std::sqrt(cx * cx + cy * cy) < 0.5f * fraction;
}

// The mean colour over the disc of 'fraction'
float3 discMean(const std::vector<float4> &image, float fraction)
{
  double sum[3] = {};
  size_t n = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      if (!inDisc(x, y, fraction))
        continue;
      const float4 p = image[size_t(y) * IMAGE_SIZE.x + x];
      for (int c = 0; c < 3; c++)
        sum[c] += p[c];
      n++;
    }
  }
  return float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(n);
}

bool bitwiseEqual(const std::vector<float4> &a, const std::vector<float4> &b)
{
  if (a.empty() || a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); i++) {
    for (int c = 0; c < 4; c++) {
      if (a[i][c] != b[i][c])
        return false;
    }
  }
  return true;
}

// Whether every component of 'mean' is within 'tolerance' (relative) of
// 'expected'
bool close(float3 mean, float3 expected, float tolerance)
{
  for (int c = 0; c < 3; c++) {
    if (std::abs(mean[c] - expected[c]) > tolerance * expected[c])
      return false;
  }
  return true;
}

// The mean of the absolute per-channel difference between two images over
// the disc of 'fraction'
float meanDifference(
    const std::vector<float4> &a, const std::vector<float4> &b, float fraction)
{
  double sum = 0.;
  size_t n = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      if (!inDisc(x, y, fraction))
        continue;
      const size_t i = size_t(y) * IMAGE_SIZE.x + x;
      for (int c = 0; c < 3; c++)
        sum += std::abs(a[i][c] - b[i][c]);
      n += 3;
    }
  }
  return float(sum / double(n));
}

// Tests //////////////////////////////////////////////////////////////////////

// A unit sphere of subsurface 'color' and 'radius' at the origin
anari::World sssSphereWorld(anari::Device d, float3 color, float radius)
{
  return makeWorld(d,
      {makeSurface(d,
          makeSphereGeometry(d, float3(0.f), 1.f),
          makeSss(d, color, radius))});
}

bool testBlack(anari::Device d)
{
  auto world = sssSphereWorld(d, float3(0.f), 0.5f);
  const auto image = render(d, world);
  anari::release(d, world);
  bool ok = !image.empty();
  for (uint32_t y = 0; ok && y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; ok && x < IMAGE_SIZE.x; x++) {
      if (!inDisc(x, y, 0.85f))
        continue;
      const float4 p = image[size_t(y) * IMAGE_SIZE.x + x];
      ok = p.x == 0.f && p.y == 0.f && p.z == 0.f && p.w == 1.f;
      if (!ok)
        std::printf("  pixel (%u, %u) is %s\n",
            x,
            y,
            str(float3(p.x, p.y, p.z)).c_str());
    }
  }
  return report("a colour of 0 absorbs everything", ok);
}

bool testFurnace(anari::Device d)
{
  bool ok = true;
  for (float radius : {0.1f, 0.5f, 2.f}) {
    auto world = sssSphereWorld(d, float3(1.f), radius);
    Settings s;
    s.ambient = 1.5f;
    s.pixelSamples = 256;
    const float3 mean = discMean(render(d, world, s), 0.85f);
    anari::release(d, world);
    // the walk loses only what its bounce limit and Chiang's 1e-6
    // absorption take
    const bool good = close(mean, float3(s.ambient), 0.005f);
    if (!good) {
      std::printf("  radius %g: mean %s, expected %g\n",
          radius,
          str(mean).c_str(),
          s.ambient);
    }
    ok &= good;
  }
  return report("a white medium renders to the ambient radiance (furnace)", ok);
}

bool testSlabReflectance(anari::Device d)
{
  // a deep box, its top at z = 0, 0.01 mean free paths per unit: the walk
  // sees a semi-infinite slab
  const float3 color(0.2f, 0.5f, 0.8f);
  auto world = makeWorld(d,
      {makeSurface(d,
          makeBoxGeometry(
              d, float3(-20.f, -20.f, -20.f), float3(20.f, 20.f, 0.f)),
          makeSss(d, color, 0.01f))});
  Settings s;
  s.pixelSamples = 256;
  const float3 mean = discMean(render(d, world, s), 0.85f);
  anari::release(d, world);
  // Chiang's fit is approximate: the walk reflects 0.81, 0.89 and 0.96 of
  // these colours (as Typhoon's walk does), so allow up to a quarter less,
  // never more
  bool ok = mean.x < mean.y && mean.y < mean.z;
  for (int c = 0; c < 3; c++)
    ok &= mean[c] > 0.75f * color[c] && mean[c] < color[c];
  if (!ok) {
    std::printf("  mean %s, expected about %s\n",
        str(mean).c_str(),
        str(color).c_str());
  }
  return report("a slab reflects about its colour (Chiang's remap)", ok);
}

bool testBounces(anari::Device d)
{
  const auto lit = [&](int maxBounces) {
    auto world = makeWorld(d,
        {makeSurface(d,
            makeSphereGeometry(d, float3(0.f), 1.f),
            makeSss(d, float3(0.9f, 0.6f, 0.3f), 0.3f))},
        {},
        {makeDirectional(d, normalize(float3(-1.f, -1.f, -2.f)))});
    Settings s;
    s.ambient = 0.f;
    s.maxBounces = maxBounces;
    s.pixelSamples = 16;
    const auto image = render(d, world, s);
    anari::release(d, world);
    return image;
  };
  const auto none = lit(0);
  const auto one = lit(1);
  const auto two = lit(2);
  bool ok = true;
  ok &= report("maxBounces 0 takes no walk",
      !none.empty() && discMean(none, 0.85f) == float3(0.f));
  const float3 mean = discMean(one, 0.85f);
  ok &= report("maxBounces 1 lights the walk's exit",
      mean.x > 0.05f && mean.y > 0.f && mean.z > 0.f);
  ok &= report("entry, walk and exit are one bounce (maxBounces 1 and 2 agree)",
      bitwiseEqual(one, two));
  return ok;
}

bool testOwnerOnly(anari::Device d)
{
  const float3 color(0.9f, 0.7f, 0.5f);
  Settings s;
  s.pixelSamples = 16;

  auto plain = sssSphereWorld(d, color, 0.4f);
  const auto reference = render(d, plain, s);
  anari::release(d, plain);

  // an opaque sphere inside, in the same group
  auto sameGroup = makeWorld(d,
      {makeSurface(
           d, makeSphereGeometry(d, float3(0.f), 1.f), makeSss(d, color, 0.4f)),
          makeSurface(d,
              makeSphereGeometry(d, float3(0.1f, 0.f, 0.f), 0.5f),
              makeMatte(d, float3(0.2f)))});
  const auto inGroup = render(d, sameGroup, s);
  anari::release(d, sameGroup);

  // and in another instance
  auto inner = makeGroup(d,
      {makeSurface(d,
          makeSphereGeometry(d, float3(0.f), 1.f),
          makeMatte(d, float3(0.2f)))});
  auto otherInstance = makeWorld(d,
      {makeSurface(
          d, makeSphereGeometry(d, float3(0.f), 1.f), makeSss(d, color, 0.4f))},
      {makeInstance(d, inner, scaleTranslate(0.5f, float3(-0.1f, 0.f, 0.f)))});
  const auto inInstance = render(d, otherInstance, s);
  anari::release(d, otherInstance);

  // a box on a floor in the plane of its bottom face: a cut-out (mask
  // opacity 0), which every other ray passes without a change, so only a
  // walk that lost the box's bottom face to the floor would differ
  const auto box = [&](bool floor) {
    std::vector<anari::Surface> surfaces = {makeSurface(d,
        makeBoxGeometry(d, float3(-0.6f, -0.6f, -1.f), float3(0.6f, 0.6f, 0.f)),
        makeSss(d, color, 0.4f))};
    if (floor) {
      auto cutOut = anari::newObject<anari::Material>(d, "matte");
      anari::setParameter(d, cutOut, "opacity", 0.f);
      anari::setParameter(d, cutOut, "alphaMode", "mask");
      anari::commitParameters(d, cutOut);
      surfaces.push_back(makeSurface(d,
          makeBoxGeometry(d, float3(-3.f, -3.f, -1.f), float3(3.f, 3.f, -1.f)),
          cutOut));
    }
    auto world = makeWorld(d, surfaces);
    const auto image = render(d, world, s);
    anari::release(d, world);
    return image;
  };
  const auto alone = box(false);
  const auto onFloor = box(true);

  bool ok = true;
  ok &= report("the walk passes other surfaces of its group",
      bitwiseEqual(reference, inGroup));
  ok &= report(
      "the walk passes other instances", bitwiseEqual(reference, inInstance));
  ok &= report("the walk finds its faces where another surface lies on them",
      bitwiseEqual(alone, onFloor));
  return ok;
}

// The walk sees only its own mesh: an SSS box of triangles (camera above,
// ambient light) renders as alone (bitwise) with other meshes inside it,
// which nothing else sees: triangle and quad boxes in its group and in
// another instance, a triangle plate just under its top face that every
// first (extended) walk ray reaches, and a smaller copy of it as a second
// transform of its own instance array (instPrimID is part of the owner),
// nor with a dense transferFunction1D volume inside it.
bool testOwnerMeshes(anari::Device d)
{
  const float3 color(0.9f, 0.7f, 0.5f);
  const float3 lo(-0.6f, -0.6f, -1.f), hi(0.6f, 0.6f, 0.f);
  const float3 innerLo(-0.3f, -0.3f, -0.7f), innerHi(0.3f, 0.3f, -0.3f);
  Settings s;
  s.pixelSamples = 16;
  const auto sssBox = [&]() {
    return makeSurface(d, makeBoxGeometry(d, lo, hi), makeSss(d, color, 0.4f));
  };
  const auto matte = [&](anari::Geometry g) {
    return makeSurface(d, g, makeMatte(d, float3(0.2f)));
  };
  const auto image = [&](anari::World world, int seed = -1) {
    Settings t = s;
    t.seed = seed;
    const auto pixels = render(d, world, t);
    anari::release(d, world);
    return pixels;
  };
  const auto alone = image(makeWorld(d, {sssBox()}));

  struct Case
  {
    const char *name;
    std::vector<float4> pixels;
  };
  std::vector<Case> cases;
  cases.push_back({"a triangle mesh in its group",
      image(makeWorld(
          d, {sssBox(), matte(makeBoxGeometry(d, innerLo, innerHi))}))});
  cases.push_back({"a quad mesh in its group",
      image(makeWorld(
          d, {sssBox(), matte(makeQuadBoxGeometry(d, innerLo, innerHi))}))});
  cases.push_back({"a triangle mesh in another instance",
      image(makeWorld(d,
          {sssBox()},
          {makeInstance(d,
              makeGroup(d, {matte(makeBoxGeometry(d, innerLo, innerHi))}),
              mat4(identity))}))});
  cases.push_back({"a quad mesh in another instance",
      image(makeWorld(d,
          {sssBox()},
          {makeInstance(d,
              makeGroup(d, {matte(makeQuadBoxGeometry(d, innerLo, innerHi))}),
              mat4(identity))}))});
  cases.push_back({"a plate its first rays reach",
      image(makeWorld(d,
          {sssBox(),
              matte(makePlateGeometry(
                  d, float2(-0.55f), float2(0.55f), -0.05f))}))});
  // the box as transforms 0 (the identity) and 1 (half its size, inside) of
  // one instance array
  const auto array = [&](std::vector<mat4> xfms) {
    return image(makeWorld(
        d, {}, {makeInstanceArray(d, makeGroup(d, {sssBox()}), xfms)}));
  };
  // a dense transferFunction1D volume inside the box: walks don't track
  // volumes (Typhoon has none), and no other ray reaches it
  const auto withVolume = [&](float3 vlo, float3 vhi) {
    return image(makeWorld(d,
        {sssBox()},
        {makeInstance(d, makeVolumeGroup(d, vlo, vhi), mat4(identity))}));
  };
  cases.push_back(
      {"a transferFunction1D volume inside it", withVolume(innerLo, innerHi)});
  // the same volume through the top face, which camera rays then cross: the
  // volume shows, so the case above isn't passed vacuously
  const auto volumeOut =
      withVolume(innerLo, float3(innerHi.x, innerHi.y, 0.3f));
  const auto arrayAlone = array({mat4(identity)});
  cases.push_back({"another transform of its instance array",
      array({mat4(identity), scaleTranslate(0.5f, float3(0.f, 0.f, -0.5f))})});

  bool ok = true;
  for (const Case &c : cases) {
    const auto &reference = &c == &cases.back() ? arrayAlone : alone;
    ok &= report(std::string("the walk passes ") + c.name,
        bitwiseEqual(reference, c.pixels));
  }
  ok &= report("a volume through its top face shows",
      !bitwiseEqual(alone, volumeOut));
  return ok;
}

bool testScale(anari::Device d)
{
  const float3 color(0.9f, 0.7f, 0.5f);
  Settings s;
  s.pixelSamples = 128;
  const auto direct = [&](float r, int seed) {
    auto world = makeWorld(d,
        {makeSurface(d,
            makeSphereGeometry(d, float3(0.f), r),
            makeSss(d, color, 0.4f * r))});
    Settings t = s;
    t.seed = seed;
    t.position = float3(0.f, 0.f, 3.f * r);
    t.height = 2.2f * r;
    const auto image = render(d, world, t);
    anari::release(d, world);
    return image;
  };
  const auto reference = direct(2.f, 0);
  const auto otherSeed = direct(2.f, 1);
  const float noise = meanDifference(reference, otherSeed, 0.85f);

  // the unit sphere, scaled by 2 by an instance
  auto group = makeGroup(d,
      {makeSurface(d,
          makeSphereGeometry(d, float3(0.f), 1.f),
          makeSss(d, color, 0.8f))});
  auto world = makeWorld(
      d, {}, {makeInstance(d, group, scaleTranslate(2.f, float3(0.f)))});
  Settings t = s;
  t.seed = 0;
  t.position = float3(0.f, 0.f, 6.f);
  t.height = 4.4f;
  const auto instanced = render(d, world, t);
  anari::release(d, world);

  // the scene at 1e-3
  const auto small = direct(2e-3f, 0);

  const float instancedDifference = meanDifference(reference, instanced, 0.85f);
  const float smallDifference = meanDifference(reference, small, 0.85f);
  const bool instancedOk = instancedDifference < 1.5f * noise;
  const bool smallOk = smallDifference < 1.5f * noise;
  if (!instancedOk || !smallOk) {
    std::printf("  noise between seeds %g, instanced %g, at 1e-3 %g\n",
        noise,
        instancedDifference,
        smallDifference);
  }
  bool ok = true;
  ok &= report("radii are world units (an instance's scale)", instancedOk);
  ok &= report("the walk works at any scale (1e-3)", smallOk);
  return ok;
}

// --bench ////////////////////////////////////////////////////////////////////

// A UV sphere of 'n' x n / 2 quads (two triangles each) of radius 'r' at
// 'center'
anari::Geometry makeTessellatedSphere(
    anari::Device d, float3 center, float r, int n)
{
  const int rings = n / 2;
  std::vector<float3> v;
  for (int j = 0; j <= rings; j++) {
    const float theta = 3.14159265f * j / rings;
    for (int i = 0; i < n; i++) {
      const float phi = 2.f * 3.14159265f * i / n;
      v.push_back(center
          + r
              * float3(std::sin(theta) * std::cos(phi),
                  std::sin(theta) * std::sin(phi),
                  std::cos(theta)));
    }
  }
  std::vector<uint3> indices;
  for (int j = 0; j < rings; j++) {
    for (int i = 0; i < n; i++) {
      const uint32_t a = j * n + i, b = j * n + (i + 1) % n;
      indices.push_back(uint3(a, a + n, b + n));
      indices.push_back(uint3(a, b + n, b));
    }
  }
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

// The median time of 7 renders of 4 samples (after a first one that builds
// the BVH) of 'world', 512 x 512, from above
double benchMilliseconds(anari::Device d, anari::World world)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  anari::setParameter(d, renderer, "ambientRadiance", 1.f);
  anari::setParameter(d, renderer, "pixelSamples", 4);
  anari::commitParameters(d, renderer);
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.2f);
  anari::commitParameters(d, camera);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", uint2(512, 512));
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::release(d, world);

  std::vector<double> times;
  for (int r = 0; r <= 7; r++) {
    const auto t0 = std::chrono::steady_clock::now();
    anari::render(d, frame);
    anari::wait(d, frame);
    const double ms = 1e3
        * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
              .count();
    if (r > 0)
      times.push_back(ms);
  }
  anari::release(d, frame);
  std::sort(times.begin(), times.end());
  return times[times.size() / 2];
}

// Times an SSS box (mean free path 0.4 of its 1.2 width: about 10 walk
// steps) alone and with a dense mesh hidden inside it, in its group: the
// walk traces the whole world and drops the inner mesh's hits in its
// filter, where Typhoon's walk traces the box's own BVH
void bench(anari::Device d)
{
  const float3 color(0.9f, 0.7f, 0.5f);
  const auto box = [&]() {
    return makeSurface(d,
        makeBoxGeometry(d, float3(-0.6f, -0.6f, -1.f), float3(0.6f, 0.6f, 0.f)),
        makeSss(d, color, 0.4f));
  };
  const double alone = benchMilliseconds(d, makeWorld(d, {box()}));
  for (int n : {64, 512}) {
    const double inner = benchMilliseconds(d,
        makeWorld(d,
            {box(),
                makeSurface(d,
                    makeTessellatedSphere(d, float3(0.f, 0.f, -0.5f), 0.4f, n),
                    makeMatte(d, float3(0.2f)))}));
    std::printf(
        "TIME SSS box, 512x512, 4 samples, median of 7 renders: alone %.1f "
        "ms, with a %d-triangle mesh inside %.1f ms (%.2fx)\n",
        alone,
        n * n,
        inner,
        inner / alone);
  }
}

} // namespace

int main(int argc, const char **argv)
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "could not load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");
  if (!d) {
    std::fprintf(stderr, "could not create the halcyon device\n");
    return 1;
  }

  bool ok = true;
  ok &= testBlack(d);
  ok &= testFurnace(d);
  ok &= testSlabReflectance(d);
  ok &= testBounces(d);
  ok &= testOwnerOnly(d);
  ok &= testOwnerMeshes(d);
  ok &= testScale(d);

  if (argc > 1 && std::strcmp(argv[1], "--bench") == 0)
    bench(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  ok &= report("no errors or warnings", g_errors == 0 && g_warnings == 0);
  return ok ? 0 : 1;
}
