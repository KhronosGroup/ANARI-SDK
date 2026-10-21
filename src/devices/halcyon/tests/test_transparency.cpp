// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders stacks of squares facing an orthographic camera (so eye-lit shading
// gives the surface color exactly) with blend and mask materials and checks
// how the default mode composites them: color and alpha, the depth and
// objectId channels (the visible hit), debug modes (the first hit,
// opaque) and volumes between the layers.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {16, 16};
// the camera looks down -z from this z; a layer at z is at depth CAMERA_Z - z
constexpr float CAMERA_Z = 5.f;
// transparent background, so the pixel's alpha is the layers' coverage
const float4 BACKGROUND = {0.f, 0.f, 1.f, 0.f};
const uint32_t MISS_ID = ~0u;

int g_errors = 0;

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
  }
}

// A square covering the view at 'z' with a matte material
struct Layer
{
  float z;
  float3 color;
  const char *alphaMode; // "opaque", "blend" or "mask"
  float opacity;
  uint32_t id;
  float alphaCutoff = 0.5f;
};

// A structured-regular volume of constant value over the view between z0 and
// z1, with a constant color and opacity
struct VolumeLayer
{
  float z0;
  float z1;
  uint32_t id;
};

anari::Surface makeSurface(anari::Device d, const Layer &l)
{
  const float3 positions[] = {
      {-1.f, -1.f, l.z}, {1.f, -1.f, l.z}, {1.f, 1.f, l.z}, {-1.f, 1.f, l.z}};
  auto geom = anari::newObject<anari::Geometry>(d, "quad");
  anari::setParameterArray1D(d, geom, "vertex.position", positions, 4);
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", l.color);
  anari::setParameter(d, mat, "alphaMode", l.alphaMode);
  anari::setParameter(d, mat, "opacity", l.opacity);
  anari::setParameter(d, mat, "alphaCutoff", l.alphaCutoff);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", l.id);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Volume makeVolume(anari::Device d, const VolumeLayer &v)
{
  const float voxels[8] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels, 2, 2, 2);
  anari::setParameter(d, field, "origin", float3(-1.f, -1.f, v.z0));
  anari::setParameter(d, field, "spacing", float3(2.f, 2.f, v.z1 - v.z0));
  anari::commitParameters(d, field);

  auto volume = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, volume, "value", field);
  anari::setParameter(d, volume, "color", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, volume, "opacity", 0.5f);
  anari::setParameter(d, volume, "id", v.id);
  anari::commitParameters(d, volume);
  return volume;
}

anari::World makeWorld(anari::Device d,
    const std::vector<Layer> &layers,
    const std::vector<VolumeLayer> &volumes = {})
{
  auto world = anari::newObject<anari::World>(d);
  std::vector<anari::Surface> surfaces;
  for (const auto &l : layers)
    surfaces.push_back(makeSurface(d, l));
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
  std::vector<anari::Volume> vols;
  for (const auto &v : volumes)
    vols.push_back(makeVolume(d, v));
  if (!vols.empty())
    anari::setParameterArray1D(d, world, "volume", vols.data(), vols.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto v : vols)
    anari::release(d, v);
  return world;
}

// The center pixel's color, depth and objectId
struct Pixel
{
  float4 color;
  float depth;
  uint32_t objectId;
};

Pixel render(anari::Device d, anari::World world, const char *mode = "default")
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, CAMERA_Z));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 1.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "mode", mode);
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "volumeSamplingRate", 1.f);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, frame, "world", world);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  const size_t center = (IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2;
  Pixel p;
  auto color = anari::map<float4>(d, frame, "channel.color");
  p.color = color.data[center];
  anari::unmap(d, frame, "channel.color");
  auto depth = anari::map<float>(d, frame, "channel.depth");
  p.depth = depth.data[center];
  anari::unmap(d, frame, "channel.depth");
  auto ids = anari::map<uint32_t>(d, frame, "channel.objectId");
  p.objectId = ids.data[center];
  anari::unmap(d, frame, "channel.objectId");

  anari::release(d, frame);
  return p;
}

bool near(float4 a, float4 b)
{
  const float4 e = linalg::abs(a - b);
  return e.x < 1e-4f && e.y < 1e-4f && e.z < 1e-4f && e.w < 1e-4f;
}

bool near(float a, float b)
{
  return std::fabs(a - b) < 1e-4f * std::max(1.f, std::fabs(b));
}

// Checks the center pixel of 'layers' against the expected color, depth
// (distance from the camera) and objectId
bool check(anari::Device d,
    const char *test,
    const std::vector<Layer> &layers,
    float4 color,
    float depth,
    uint32_t objectId,
    const char *mode = "default")
{
  auto world = makeWorld(d, layers);
  const Pixel p = render(d, world, mode);
  anari::release(d, world);

  bool pass = true;
  if (color.w >= 0.f && !near(p.color, color)) {
    std::printf("FAIL %s: color (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
        test,
        p.color.x,
        p.color.y,
        p.color.z,
        p.color.w,
        color.x,
        color.y,
        color.z,
        color.w);
    pass = false;
  }
  if (!near(p.depth, depth)) {
    std::printf("FAIL %s: depth %f, expected %f\n", test, p.depth, depth);
    pass = false;
  }
  if (p.objectId != objectId) {
    std::printf(
        "FAIL %s: objectId %u, expected %u\n", test, p.objectId, objectId);
    pass = false;
  }
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

const float3 RED = {1.f, 0.f, 0.f};
const float3 GREEN = {0.f, 1.f, 0.f};
const float3 WHITE = {1.f, 1.f, 1.f};
// a color check to skip (debug modes' colors are tested elsewhere)
const float4 ANY_COLOR = {0.f, 0.f, 0.f, -1.f};

bool testSurfaces(anari::Device d)
{
  bool pass = true;

  // 0.25 red over the transparent blue background
  pass &= check(d,
      "blend over background",
      {{0.f, RED, "blend", 0.25f, 1}},
      float4(0.25f, 0.f, 0.75f, 0.25f),
      CAMERA_Z,
      1);

  pass &= check(d,
      "blend over opaque",
      {{1.f, RED, "blend", 0.5f, 1}, {0.f, GREEN, "opaque", 1.f, 2}},
      float4(0.5f, 0.5f, 0.f, 1.f),
      CAMERA_Z - 1.f,
      1);

  // front to back: 0.5 red, 0.25 green, 0.25 white; given in another order
  pass &= check(d,
      "blend layers",
      {{0.f, WHITE, "opaque", 1.f, 3},
          {2.f, RED, "blend", 0.5f, 1},
          {1.f, GREEN, "blend", 0.5f, 2}},
      float4(0.75f, 0.5f, 0.25f, 1.f),
      CAMERA_Z - 2.f,
      1);

  pass &= check(d,
      "opaque in front hides the rest",
      {{1.f, RED, "opaque", 0.25f, 1}, {0.f, GREEN, "blend", 0.5f, 2}},
      float4(1.f, 0.f, 0.f, 1.f),
      CAMERA_Z - 1.f,
      1);

  // opacity below the cutoff: cut out, so depth and id come from behind
  pass &= check(d,
      "mask cut out",
      {{1.f, RED, "mask", 0.3f, 1}, {0.f, GREEN, "opaque", 1.f, 2}},
      float4(0.f, 1.f, 0.f, 1.f),
      CAMERA_Z,
      2);

  pass &= check(d,
      "mask kept",
      {{1.f, RED, "mask", 0.7f, 1}, {0.f, GREEN, "opaque", 1.f, 2}},
      float4(1.f, 0.f, 0.f, 1.f),
      CAMERA_Z - 1.f,
      1);

  // nothing visible: the miss depth and id, background color
  pass &= check(d,
      "mask cut out over background",
      {{1.f, RED, "mask", 0.3f, 1}},
      BACKGROUND,
      3.402823466e+38f,
      MISS_ID);

  pass &= check(d,
      "invisible blend skipped",
      {{1.f, RED, "blend", 0.f, 1}, {0.f, GREEN, "opaque", 1.f, 2}},
      float4(0.f, 1.f, 0.f, 1.f),
      CAMERA_Z,
      2);

  // debug modes show the first hit, opaque, as in helide
  pass &= check(d,
      "debug mode ignores mask",
      {{1.f, RED, "mask", 0.3f, 1}, {0.f, GREEN, "opaque", 1.f, 2}},
      ANY_COLOR,
      CAMERA_Z - 1.f,
      1,
      "Ng");
  pass &= check(d,
      "debug mode pixel stays opaque",
      {{1.f, RED, "blend", 0.25f, 1}},
      float4(0.f, 1.f, 0.f, 1.f),
      CAMERA_Z - 1.f,
      1,
      "hitSurface");

  // many layers: the loop reaches every one and stops at the miss
  std::vector<Layer> stack;
  const int n = 200;
  for (int i = 0; i < n; i++)
    stack.push_back({1.f - 2.f * i / n, RED, "blend", 0.02f, uint32_t(i)});
  const float coverage = 1.f - std::pow(0.98f, float(n));
  pass &= check(d,
      "200 blend layers",
      stack,
      float4(coverage, 0.f, 1.f - coverage, coverage),
      CAMERA_Z - 1.f,
      0);

  return pass;
}

// The center pixel of a world of 'layers' and 'volumes'
Pixel renderVolumes(anari::Device d,
    const std::vector<Layer> &layers,
    const std::vector<VolumeLayer> &volumes)
{
  auto world = makeWorld(d, layers, volumes);
  const Pixel p = render(d, world);
  anari::release(d, world);
  return p;
}

bool testVolumes(anari::Device d)
{
  bool pass = true;
  const auto report = [&](const char *test, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", test);
    pass &= ok;
  };

  const Layer blend = {1.f, RED, "blend", 0.5f, 1};
  const VolumeLayer behind = {-1.f, 0.f, 7};
  const VolumeLayer inFront = {2.f, 3.f, 7};

  // a volume behind a blend surface shows through it; depth and id are the
  // surface's
  const Pixel surfaceOnly = renderVolumes(d, {blend}, {});
  const Pixel withBehind = renderVolumes(d, {blend}, {behind});
  report("volume behind blend shows",
      withBehind.color.y > 0.01f && near(withBehind.depth, CAMERA_Z - 1.f)
          && withBehind.objectId == 1
          && withBehind.color.w > surfaceOnly.color.w);

  // behind an opaque surface it doesn't
  const Layer opaque = {1.f, RED, "opaque", 1.f, 1};
  const Pixel opaqueBehind = renderVolumes(d, {opaque}, {behind});
  report("volume behind opaque hidden",
      near(opaqueBehind.color, float4(1.f, 0.f, 0.f, 1.f))
          && opaqueBehind.objectId == 1);

  // a volume in front is the visible hit
  const Pixel withInFront = renderVolumes(d, {blend}, {inFront});
  report("volume in front is first hit",
      withInFront.objectId == 7 && near(withInFront.depth, CAMERA_Z - 3.f));

  // a volume spanning a blend surface is marched on both sides of it
  const Pixel frontPart = renderVolumes(d, {blend}, {{1.f, 2.f, 7}});
  const Pixel spanning = renderVolumes(d, {blend}, {{0.f, 2.f, 7}});
  report("volume continues behind blend",
      spanning.color.w > frontPart.color.w + 1e-3f && spanning.objectId == 7
          && near(spanning.depth, CAMERA_Z - 2.f));

  return pass;
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

  bool pass = testSurfaces(d);
  pass &= testVolumes(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_errors != 0) {
    std::printf("FAIL %d device errors\n", g_errors);
    pass = false;
  }
  return pass ? 0 : 1;
}
