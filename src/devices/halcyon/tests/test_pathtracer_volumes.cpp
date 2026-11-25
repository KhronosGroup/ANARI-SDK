// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// transferFunction1D volumes in the 'quality' (renderer/pathtracer/
// VolumeTransport.isph): scattering media of extinction
// sigma_t = -ln(1 - alpha) / unitDistance and single-scattering albedo the
// transfer function's colour, tracked by delta tracking along paths and by
// ratio tracking along shadow rays and emitter-only segments. Typhoon has no
// grid volumes, so the oracles are the 'preview' renderer (alpha, depth,
// ids) and analytic scenes, within noise:
//
// - A box of constant density seen straight through: alpha is
//   1 - (1 - alpha)^(d / unitDistance) (and the background shows through by
//   the transmittance), as the 'preview' renderer composites it; the same
//   through an instance and over a field whose density varies.
// - A thin slab under a directional light with maxBounces 0 shows only
//   single scattering: c E (1 - exp(-2 sigma_t h)) / (8 pi) from above, and
//   c E (1 - exp(-sigma_t h)) / (4 pi) with 'disableShadows'; a floor under
//   it receives the light through the slab's transmittance.
// - A floor under an absorbing slab lit by the ambient light (sampled by
//   shadow rays and gathered by the emitter-only segment, through ratio
//   tracking) shows
//   a A 2 E3(sigma_t h).
// - Two overlapping volumes render as one of their summed extinction and
//   extinction-weighted colour.
// - A dense volume is the first non-specular hit of the albedo and normal
//   channels: its colour and 0.
// - Depth and ids are the 'preview' renderer's.
//
// Usage: halcyon_test_pathtracer_volumes [--bench]
// --bench also times renders of a sparse volume (a few dense blobs in a
// large, mostly empty field).

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
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr float PI = 3.14159265358979323846f;
constexpr uint2 IMAGE_SIZE = {32, 32};

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
    g_errors++;
  }
}

bool report(const std::string &name, bool ok, const std::string &detail = {})
{
  std::printf("%s %s%s%s\n",
      ok ? "PASS" : "FAIL",
      name.c_str(),
      detail.empty() ? "" : ": ",
      detail.c_str());
  return ok;
}

std::string str(float v)
{
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

std::string str(float3 v)
{
  return "(" + str(v.x) + ", " + str(v.y) + ", " + str(v.z) + ")";
}

// Scene //////////////////////////////////////////////////////////////////////

// A structuredRegular field over the box [lower, upper] of n^3 voxels, the
// voxel at (i, j, k) of value 'value(i / (n - 1), j / (n - 1), k / (n - 1))'
anari::SpatialField makeField(anari::Device d,
    float3 lower,
    float3 upper,
    const std::function<float(float3)> &value = {},
    int n = 2)
{
  std::vector<float> voxels(size_t(n) * n * n);
  for (int k = 0; k < n; k++) {
    for (int j = 0; j < n; j++) {
      for (int i = 0; i < n; i++) {
        const float3 p = float3(float(i), float(j), float(k)) / float(n - 1);
        voxels[(size_t(k) * n + j) * n + i] = value ? value(p) : 1.f;
      }
    }
  }
  auto f = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, f, "data", voxels.data(), n, n, n);
  anari::setParameter(d, f, "origin", lower);
  anari::setParameter(d, f, "spacing", (upper - lower) / float(n - 1));
  anari::commitParameters(d, f);
  return f;
}

// A transferFunction1D volume of uniform 'color' and 'opacity' over 'field'
// (released)
anari::Volume makeVolume(anari::Device d,
    anari::SpatialField field,
    float3 color,
    float opacity,
    float unitDistance = 1.f,
    uint32_t id = 0)
{
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", color);
  anari::setParameter(d, vol, "opacity", opacity);
  anari::setParameter(d, vol, "unitDistance", unitDistance);
  anari::setParameter(d, vol, "id", id);
  anari::commitParameters(d, vol);
  return vol;
}

// A box volume of constant density
anari::Volume boxVolume(anari::Device d,
    float3 lower,
    float3 upper,
    float3 color,
    float opacity,
    float unitDistance = 1.f,
    uint32_t id = 0)
{
  return makeVolume(
      d, makeField(d, lower, upper), color, opacity, unitDistance, id);
}

anari::Material matte(anari::Device d, float3 color)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  return mat;
}

// A matte quad z = 'z' over [-extent, extent]^2, facing +z
anari::Surface floorQuad(
    anari::Device d, float z, float extent, float3 color, uint32_t id = 0)
{
  const std::vector<float3> v = {{-extent, -extent, z},
      {extent, -extent, z},
      {extent, extent, z},
      {-extent, extent, z}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", matte(d, color));
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

anari::Light directionalLight(
    anari::Device d, float3 direction, float irradiance)
{
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", irradiance);
  anari::commitParameters(d, light);
  return light;
}

// A world of 'volumes', 'surfaces', 'lights' and 'instances' (all released)
anari::World makeWorld(anari::Device d,
    const std::vector<anari::Volume> &volumes,
    const std::vector<anari::Surface> &surfaces = {},
    const std::vector<anari::Light> &lights = {},
    const std::vector<anari::Instance> &instances = {})
{
  auto world = anari::newObject<anari::World>(d);
  if (!volumes.empty())
    anari::setParameterArray1D(d, world, "volume", volumes.data(), volumes.size());
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  if (!instances.empty()) {
    anari::setParameterArray1D(
        d, world, "instance", instances.data(), instances.size());
  }
  anari::commitParameters(d, world);
  for (auto v : volumes)
    anari::release(d, v);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto l : lights)
    anari::release(d, l);
  for (auto i : instances)
    anari::release(d, i);
  return world;
}

// Orthographic camera at height 'z' looking down -z at the [-1, 1]^2 square
anari::Camera topCamera(anari::Device d, float z = 3.f)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, z));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

using SetParams = std::function<void(anari::Device, anari::Renderer)>;

struct Image
{
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> objectId;
  std::vector<float3> albedo;
  std::vector<float3> normal;
};

template <typename T>
std::vector<T> readChannel(
    anari::Device d, anari::Frame frame, const char *channel, uint2 size)
{
  std::vector<T> values;
  auto fb = anari::map<T>(d, frame, channel);
  if (fb.data && fb.width == size.x && fb.height == size.y)
    values.assign(fb.data, fb.data + size_t(size.x) * size.y);
  anari::unmap(d, frame, channel);
  return values;
}

// Renders 'world' through 'camera' (released) with a renderer of 'subtype'
// set up by 'setParams' (the 'quality' with 'pixelSamples' paths per
// pixel), with the albedo and normal channels if 'albedoNormal'
Image render(anari::Device d,
    const char *subtype,
    anari::World world,
    anari::Camera camera,
    int pixelSamples,
    float4 background,
    const SetParams &setParams = {},
    bool albedoNormal = false)
{
  const bool pathtracer = std::string(subtype) == "quality";
  auto renderer = anari::newObject<anari::Renderer>(d, subtype);
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  if (pathtracer)
    anari::setParameter(d, renderer, "pixelSamples", pixelSamples);
  else {
    anari::setParameter(d, renderer, "ambientOcclusion", false);
    // steps of 0.025 or less through the fields below, to keep the march's
    // error small
    anari::setParameter(d, renderer, "volumeSamplingRate", 4.f);
  }
  anari::setParameter(d, renderer, "background", background);
  if (setParams)
    setParams(d, renderer);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  if (albedoNormal) {
    anari::setParameter(d, frame, "channel.albedo", ANARI_FLOAT32_VEC3);
    anari::setParameter(d, frame, "channel.normal", ANARI_FLOAT32_VEC3);
  }
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  Image image;
  image.color = readChannel<float4>(d, frame, "channel.color", IMAGE_SIZE);
  image.depth = readChannel<float>(d, frame, "channel.depth", IMAGE_SIZE);
  image.objectId =
      readChannel<uint32_t>(d, frame, "channel.objectId", IMAGE_SIZE);
  if (albedoNormal) {
    image.albedo = readChannel<float3>(d, frame, "channel.albedo", IMAGE_SIZE);
    image.normal = readChannel<float3>(d, frame, "channel.normal", IMAGE_SIZE);
  }
  anari::release(d, frame);
  return image;
}

// The mean rgb and alpha over the pixels with x in [x0, x1)
float4 mean(const std::vector<float4> &pixels, int x0 = 0, int x1 = 32)
{
  double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
  size_t n = 0;
  for (size_t i = 0; i < pixels.size(); i++) {
    const int x = int(i % IMAGE_SIZE.x);
    if (x < x0 || x >= x1)
      continue;
    r += pixels[i].x;
    g += pixels[i].y;
    b += pixels[i].z;
    a += pixels[i].w;
    n++;
  }
  const double s = n == 0 ? 0.0 : 1.0 / double(n);
  return n == 0 ? float4(NAN)
                : float4(float(r * s), float(g * s), float(b * s), float(a * s));
}

float3 rgb(float4 c)
{
  return float3(c.x, c.y, c.z);
}

bool allFinite(const std::vector<float4> &pixels)
{
  if (pixels.empty())
    return false;
  for (const auto &p : pixels) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
        || !std::isfinite(p.w) || p.x < 0.f || p.y < 0.f || p.z < 0.f)
      return false;
  }
  return true;
}

bool nearRelative(float3 a, float3 b, float tolerance)
{
  return maxelem(abs(a - b)) <= tolerance * maxelem(abs(b));
}

float extinction(float opacity, float unitDistance)
{
  return -std::log(1.f - opacity) / unitDistance;
}

// Transmittance //////////////////////////////////////////////////////////////

// A box [-1, 1]^3 seen straight down through its depth 2: alpha is
// 1 - (1 - opacity)^(2 / unitDistance) over a transparent background, which
// shows through by the transmittance; the 'preview' renderer composites the
// same opacity. A field whose values vary (the opacity growing with x), and
// one instanced at half size, give the 'preview' renderer's alpha too.
bool testTransmittance(anari::Device d)
{
  bool pass = true;
  const float4 background(1.f, 1.f, 1.f, 0.f);
  for (auto [opacity, unitDistance] : {std::pair{0.5f, 1.f},
           std::pair{0.5f, 0.5f},
           std::pair{0.2f, 2.f}}) {
    auto world = makeWorld(d,
        {boxVolume(d,
            float3(-1.f),
            float3(1.f),
            float3(1.f),
            opacity,
            unitDistance)});
    const float T = std::pow(1.f - opacity, 2.f / unitDistance);
    const std::string name = "box of opacity " + str(opacity)
        + ", unitDistance " + str(unitDistance);

    // 64 samples of 32 x 32 pixels: the alpha's standard error is about
    // 0.002
    const Image pt =
        render(d, "quality", world, topCamera(d), 64, background);
    const float4 m = mean(pt.color);
    pass &= report(name + ": alpha 1 - T, background times T",
        allFinite(pt.color) && std::abs(m.w - (1.f - T)) < 0.01f
            && nearRelative(rgb(m), float3(T), 0.03f),
        "alpha " + str(m.w) + ", rgb " + str(rgb(m)) + " vs T " + str(T));

    const Image def = render(d, "default", world, topCamera(d), 1, background);
    const float4 md = mean(def.color);
    pass &= report(name + ": the 'preview' renderer's alpha",
        std::abs(m.w - md.w) < 0.01f,
        str(m.w) + " vs " + str(md.w));
    anari::release(d, world);
  }

  // a field whose value goes 0 to 1 along x, through an opacity ramp, in an
  // instance scaled by 0.5: compared strip by strip along x
  {
    auto field = makeField(
        d, float3(-1.f), float3(1.f), [](float3 p) { return p.x; }, 11);
    auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
    anari::setAndReleaseParameter(d, vol, "value", field);
    anari::setParameter(d, vol, "color", float3(0.5f));
    const std::vector<float> ramp = {0.f, 0.3f, 0.9f};
    anari::setParameterArray1D(d, vol, "opacity", ramp.data(), ramp.size());
    anari::commitParameters(d, vol);
    auto group = anari::newObject<anari::Group>(d);
    anari::setParameterArray1D(d, group, "volume", &vol, 1);
    anari::commitParameters(d, group);
    anari::release(d, vol);
    auto inst = anari::newObject<anari::Instance>(d, "transform");
    anari::setParameter(d, inst, "transform", linalg::scaling_matrix(float3(0.5f)));
    anari::setAndReleaseParameter(d, inst, "group", group);
    anari::commitParameters(d, inst);
    auto world = makeWorld(d, {}, {}, {}, {inst});

    const Image pt =
        render(d, "quality", world, topCamera(d), 64, background);
    const Image def = render(d, "default", world, topCamera(d), 1, background);
    bool ok = allFinite(pt.color);
    std::string detail;
    // the box covers x in [-0.5, 0.5], pixels 8 to 23
    for (int x0 = 8; x0 < 24; x0 += 4) {
      const float a = mean(pt.color, x0, x0 + 4).w;
      const float b = mean(def.color, x0, x0 + 4).w;
      ok &= std::abs(a - b) < 0.015f;
      detail += str(a) + " vs " + str(b) + "; ";
    }
    pass &= report(
        "varying field in a scaled instance: the 'preview' renderer's alpha",
        ok,
        detail);
    anari::release(d, world);
  }
  return pass;
}

// Single scattering //////////////////////////////////////////////////////////

constexpr float SLAB_HEIGHT = 0.25f;
constexpr float SLAB_OPACITY = 0.5f;
const float3 SLAB_COLOR = {0.8f, 0.5f, 0.2f};

// A slab z in [0, SLAB_HEIGHT], wide enough to be infinite, of opacity
// SLAB_OPACITY and colour 'color'
anari::Volume slab(anari::Device d, float3 color = SLAB_COLOR, float z0 = 0.f)
{
  return boxVolume(d,
      float3(-50.f, -50.f, z0),
      float3(50.f, 50.f, z0 + SLAB_HEIGHT),
      color,
      SLAB_OPACITY);
}

// The slab lit straight down with irradiance 8 pi, seen straight down with
// maxBounces 0, so only single scattering: c (1 - exp(-2 sigma_t h)) (light
// and view both attenuated down to the scattering depth), or with
// 'disableShadows' 2 c (1 - exp(-sigma_t h)); a floor of albedo a under it
// adds 8 a exp(-2 sigma_t h)
bool testSingleScattering(anari::Device d)
{
  bool pass = true;
  const float sigma = extinction(SLAB_OPACITY, 1.f);
  const float E = 8.f * PI;
  const float floorAlbedo = 0.5f;
  const float T = std::exp(-sigma * SLAB_HEIGHT);
  struct Case
  {
    std::string name;
    bool floor;
    bool disableShadows;
    float3 expected;
  };
  const Case cases[] = {
      {"single scattering in a slab", false, false, SLAB_COLOR * (1.f - T * T)},
      {"single scattering in a slab without shadows",
          false,
          true,
          2.f * SLAB_COLOR * (1.f - T)},
      {"a floor lit through a slab",
          true,
          false,
          SLAB_COLOR * (1.f - T * T) + float3(8.f * floorAlbedo * T * T)},
  };
  for (const auto &c : cases) {
    std::vector<anari::Surface> surfaces;
    if (c.floor)
      surfaces.push_back(floorQuad(d, -1.f, 50.f, float3(floorAlbedo)));
    auto world = makeWorld(d,
        {slab(d)},
        surfaces,
        {directionalLight(d, float3(0.f, 0.f, -1.f), E)});
    // 256 samples of 32 x 32 pixels: about 1% noise on the scattering, as
    // only 16% of the paths scatter
    const Image pt = render(d,
        "quality",
        world,
        topCamera(d),
        256,
        float4(0.f, 0.f, 0.f, 1.f),
        [&](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "maxBounces", 0);
          anari::setParameter(d, r, "disableShadows", c.disableShadows);
        });
    const float3 m = rgb(mean(pt.color));
    pass &= report(c.name,
        allFinite(pt.color) && nearRelative(m, c.expected, 0.03f),
        str(m) + " vs " + str(c.expected));
    anari::release(d, world);
  }
  return pass;
}

// Emitter-only segments //////////////////////////////////////////////////////

// a A 2 E3(x): the ambient light A reflected by a Lambertian floor of
// albedo a under an absorbing slab of optical depth x,
// 2 E3(x) = 2 int_0^1 mu exp(-x / mu) dmu
float ambientThroughSlab(float x)
{
  const int n = 100000;
  double sum = 0.0;
  for (int i = 0; i < n; i++) {
    const double mu = (i + 0.5) / n;
    sum += mu * std::exp(-x / mu);
  }
  return float(2.0 * sum / n);
}

// A floor z = 0 of albedo a under a black (absorbing) slab z in [1, 1.25],
// lit only by the ambient light and seen from below the slab with
// maxBounces 0: its light reaches it by next-event estimation (shadow rays)
// and the emitter-only segment, MIS-weighted, both ratio-tracking through
// the slab
bool testEmitterOnlySegment(anari::Device d)
{
  const float floorAlbedo = 0.5f;
  auto world = makeWorld(d,
      {slab(d, float3(0.f), 1.f)},
      {floorQuad(d, 0.f, 50.f, float3(floorAlbedo))});
  const Image pt = render(d,
      "quality",
      world,
      topCamera(d, 0.5f),
      64,
      float4(0.f, 0.f, 0.f, 1.f),
      [](anari::Device d, anari::Renderer r) {
        anari::setParameter(d, r, "maxBounces", 0);
        anari::setParameter(d, r, "ambientRadiance", 1.f);
      });
  const float expected = floorAlbedo
      * ambientThroughSlab(extinction(SLAB_OPACITY, 1.f) * SLAB_HEIGHT);
  const float3 m = rgb(mean(pt.color));
  const bool pass = report("ambient light through an absorbing slab",
      allFinite(pt.color) && nearRelative(m, float3(expected), 0.02f),
      str(m) + " vs " + str(expected));
  anari::release(d, world);
  return pass;
}

// Overlapping volumes ////////////////////////////////////////////////////////

// Two volumes over one box add their extinctions: they render as one volume
// of opacity 1 - (1 - o1)(1 - o2) whose colour is theirs weighed by their
// extinctions, lit from above and seen from above with every bounce
bool testOverlap(anari::Device d)
{
  const float o1 = 0.3f, o2 = 0.6f;
  const float3 c1(0.9f, 0.2f, 0.1f), c2(0.1f, 0.5f, 0.9f);
  const float s1 = extinction(o1, 1.f), s2 = extinction(o2, 1.f);
  const float3 lower(-50.f, -50.f, -1.f), upper(50.f, 50.f, 1.f);
  const auto light = [&] {
    return directionalLight(d, normalize(float3(0.3f, 0.f, -1.f)), PI);
  };
  auto both = makeWorld(d,
      {boxVolume(d, lower, upper, c1, o1), boxVolume(d, lower, upper, c2, o2)},
      {},
      {light()});
  auto single = makeWorld(d,
      {boxVolume(d,
          lower,
          upper,
          (c1 * s1 + c2 * s2) / (s1 + s2),
          1.f - (1.f - o1) * (1.f - o2))},
      {},
      {light()});
  const float4 background(0.f, 0.f, 0.f, 0.f);
  const Image a = render(d, "quality", both, topCamera(d), 128, background);
  const Image b =
      render(d, "quality", single, topCamera(d), 128, background);
  const float4 ma = mean(a.color), mb = mean(b.color);
  const bool pass = report("overlapping volumes add their extinctions",
      allFinite(a.color) && nearRelative(rgb(ma), rgb(mb), 0.03f)
          && std::abs(ma.w - mb.w) < 0.01f,
      str(rgb(ma)) + ", alpha " + str(ma.w) + " vs " + str(rgb(mb))
          + ", alpha " + str(mb.w));
  anari::release(d, both);
  anari::release(d, single);
  return pass;
}

// Macrocells /////////////////////////////////////////////////////////////////

// A field of 16 macrocells along each axis (129^3 voxels over [-1, 1]^3),
// dense only in a slab across part of it, seen straight down: alpha strip
// by strip along x is the 'preview' renderer's, but for the strips next to
// the slab's edges, where the opacity ramps up between two voxels within a
// pixel, whose mean the pathtracer shows and whose centre the 'preview'
// renderer. Then the transfer function grows denser and
// the slab moves to other cells: both rebuild the macrocell majorants (stale
// ones would bound the extinction too low, or leave the slab's new cells
// empty, and the alpha there would fall short).
bool testMacrocells(anari::Device d)
{
  const auto slabField = [](float x0, float x1) {
    return [=](float3 p) { return p.x >= x0 && p.x <= x1 ? 1.f : 0.f; };
  };
  constexpr int N = 129;
  auto field = makeField(
      d, float3(-1.f), float3(1.f), slabField(0.5f, 0.75f), N);
  anari::retain(d, field);
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", float3(0.5f));
  const auto setOpacity = [&](float opacity) {
    const std::vector<float> ramp = {0.f, opacity};
    anari::setParameterArray1D(d, vol, "opacity", ramp.data(), ramp.size());
    anari::commitParameters(d, vol);
  };
  setOpacity(0.3f);
  anari::retain(d, vol);
  auto world = makeWorld(d, {vol});
  const float4 background(1.f, 1.f, 1.f, 0.f);

  bool pass = true;
  // 'edges': the first pixels of the strips next to the slab's edges
  const auto compare = [&](const std::string &name, int edge0, int edge1) {
    const Image pt =
        render(d, "quality", world, topCamera(d), 64, background);
    const Image def = render(d, "default", world, topCamera(d), 1, background);
    bool ok = allFinite(pt.color);
    std::string detail;
    float largest = 0.f;
    for (int x0 = 0; x0 < 32; x0 += 4) {
      if (x0 == edge0 || x0 == edge1)
        continue;
      const float a = mean(pt.color, x0, x0 + 4).w;
      const float b = mean(def.color, x0, x0 + 4).w;
      ok &= std::abs(a - b) < 0.015f;
      largest = std::max(largest, b);
      detail += str(a) + " vs " + str(b) + "; ";
    }
    // the slab must show
    ok &= largest > 0.25f;
    pass &= report("macrocells: " + name + ": the 'preview' renderer's alpha",
        ok,
        detail);
  };
  // the slab covers x in [0, 0.5], pixels 16 to 23
  compare("a slab in a sparse field", 12, 24);
  setOpacity(0.9f);
  compare("a denser transfer function", 12, 24);
  {
    // the slab moved to x in [-0.5, 0], pixels 8 to 15
    std::vector<float> voxels(size_t(N) * N * N);
    const auto value = slabField(0.25f, 0.5f);
    for (int k = 0; k < N; k++)
      for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
          voxels[(size_t(k) * N + j) * N + i] =
              value(float3(float(i), float(j), float(k)) / float(N - 1));
    anari::setParameterArray3D(d, field, "data", voxels.data(), N, N, N);
    anari::commitParameters(d, field);
  }
  compare("a field that changes", 4, 16);
  anari::release(d, field);
  anari::release(d, vol);
  anari::release(d, world);
  return pass;
}

// Channels ///////////////////////////////////////////////////////////////////

// A volume so dense that every camera path scatters at its top: the albedo
// channel shows its colour and the normal channel 0
bool testAlbedoNormal(anari::Device d)
{
  const float3 color(0.3f, 0.6f, 0.9f);
  auto world = makeWorld(d,
      {boxVolume(d, float3(-1.f), float3(1.f), color, 0.9999f, 0.001f)},
      {},
      {directionalLight(d, float3(0.f, 0.f, -1.f), 1.f)});
  const Image pt = render(d,
      "quality",
      world,
      topCamera(d),
      4,
      float4(0.f, 0.f, 0.f, 1.f),
      {},
      true);
  float albedoError = pt.albedo.empty() ? INFINITY : 0.f;
  for (const auto &a : pt.albedo)
    albedoError = std::max(albedoError, maxelem(abs(a - color)));
  float normalError = pt.normal.empty() ? INFINITY : 0.f;
  for (const auto &n : pt.normal)
    normalError = std::max(normalError, maxelem(abs(n)));
  const bool pass = report("albedo and normal at a volume scatter",
      albedoError < 1e-5f && normalError == 0.f,
      "albedo error " + str(albedoError) + ", normal " + str(normalError));
  anari::release(d, world);
  return pass;
}

// A volume (id 7) in front of part of a floor (id 3): the 'quality''s
// depth and object ids are the 'preview' renderer's
bool testDepthAndIds(anari::Device d)
{
  auto world = makeWorld(d,
      {boxVolume(d,
          float3(-1.f, -1.f, 0.f),
          float3(0.f, 1.f, 1.f),
          float3(1.f),
          0.5f,
          1.f,
          7)},
      {floorQuad(d, -0.5f, 0.75f, float3(0.5f), 3)});
  const float4 background(0.f, 0.f, 0.f, 1.f);
  const Image pt = render(d, "quality", world, topCamera(d), 1, background);
  const Image def = render(d, "default", world, topCamera(d), 1, background);
  bool seesBoth = false, seesVolume = false;
  for (uint32_t id : def.objectId) {
    seesBoth |= id == 3;
    seesVolume |= id == 7;
  }
  const bool pass = report("depth and ids as the 'preview' renderer's",
      !pt.depth.empty() && pt.depth == def.depth && pt.objectId == def.objectId
          && seesBoth && seesVolume);
  anari::release(d, world);
  return pass;
}

// --bench ////////////////////////////////////////////////////////////////////

// A sparse volume: a 256^3 field over [-1, 1]^3, 0 but for a few dense
// blobs (values 1 at their centres, 0 beyond their radius), over a floor
// lit by a directional light, seen in perspective
anari::World sparseWorld(anari::Device d)
{
  const float4 blobs[] = {{-0.5f, -0.4f, 0.3f, 0.15f},
      {0.4f, 0.5f, -0.2f, 0.1f},
      {0.1f, -0.2f, -0.6f, 0.12f},
      {0.6f, -0.6f, 0.6f, 0.08f}};
  auto field = makeField(
      d,
      float3(-1.f),
      float3(1.f),
      [&](float3 p) {
        const float3 x = 2.f * p - 1.f;
        float v = 0.f;
        for (const auto &b : blobs) {
          const float r = length(x - float3(b.x, b.y, b.z)) / b.w;
          v = std::max(v, std::max(0.f, 1.f - r * r));
        }
        return v;
      },
      256);
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", float3(0.8f, 0.7f, 0.6f));
  const std::vector<float> opacities = {0.f, 0.99f};
  anari::setParameterArray1D(
      d, vol, "opacity", opacities.data(), opacities.size());
  anari::setParameter(d, vol, "unitDistance", 0.02f);
  anari::commitParameters(d, vol);
  return makeWorld(d,
      {vol},
      {floorQuad(d, -1.f, 4.f, float3(0.5f))},
      {directionalLight(d, normalize(float3(0.3f, 0.2f, -1.f)), PI)});
}

double renderMilliseconds(anari::Device d, anari::Frame frame)
{
  const auto t0 = std::chrono::steady_clock::now();
  anari::render(d, frame);
  anari::wait(d, frame);
  return 1e3
      * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
}

void bench(anari::Device d)
{
  const uint2 size(512, 512);
  const int pixelSamples = 4, maxBounces = 8;
  auto world = sparseWorld(d);
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(2.f, -3.f, 2.f));
  anari::setParameter(
      d, camera, "direction", normalize(float3(-2.f, 3.f, -2.2f)));
  anari::setParameter(d, camera, "up", float3(0.f, 0.f, 1.f));
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "pixelSamples", pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", maxBounces);
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  anari::commitParameters(d, renderer);
  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  // the first render finalizes the scene and is reported apart; each later
  // one adds 'pixelSamples' samples
  const double first = renderMilliseconds(d, frame);
  const int renders = 7;
  std::vector<double> times;
  for (int r = 0; r < renders; r++)
    times.push_back(renderMilliseconds(d, frame));
  std::sort(times.begin(), times.end());
  // the mean over every pixel (x0 and x1 bound x modulo 32)
  const float3 m =
      rgb(mean(readChannel<float4>(d, frame, "channel.color", size)));
  std::printf(
      "TIME sparse volume (256^3 field), %ux%u, %d samples, maxBounces %d: "
      "first render (with finalize) %.1f ms, median of %d renders %.1f ms "
      "(mean colour %s)\n",
      size.x,
      size.y,
      pixelSamples,
      maxBounces,
      first,
      renders,
      times[renders / 2],
      str(m).c_str());
  anari::release(d, frame);
}

} // namespace

int main(int argc, const char **argv)
{
  const bool benchmark = argc > 1 && std::strcmp(argv[1], "--bench") == 0;

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = testTransmittance(d);
  pass &= testSingleScattering(d);
  pass &= testEmitterOnlySegment(d);
  pass &= testOverlap(d);
  pass &= testMacrocells(d);
  pass &= testAlbedoNormal(d);
  pass &= testDepthAndIds(d);
  if (benchmark)
    bench(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors or warnings", g_errors == 0);
  return pass ? 0 : 1;
}
