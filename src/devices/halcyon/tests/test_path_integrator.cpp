// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The pathtracer's path integrator (renderer/pathtracer/PathTracer.ispc,
// Typhoon's _IntegratePath()) against analytic values, and its convergence:
//
// - Inside a diffuse sphere of albedo a lit by a point light at its center,
//   every wall point receives the same direct light and every bounce scales
//   the path by a, so the wall shows a E / pi times 1 + a + ... + a^m after m
//   bounces, exactly up to the bounce where Russian roulette starts, and in
//   expectation after it (a E / (pi (1 - a)) for many bounces).
// - A sphere light of radiance L and radius r at the room's center gives the
//   wall the irradiance pi L (r / R)^2; with one vertex that is found by light
//   sampling and by BSDF samples hitting the light, so it is only right if
//   MIS weighs the two to one, for every number of light samples.
// - A floor under a directional light keeps 1 - opacity of the light behind a
//   'blend' blocker, nothing behind an opaque one, all of it behind a 'mask'
//   cut-out or with 'disableShadows'; 'fireflyClampThreshold' caps the
//   luminance a light sample adds.
// - A 'blend' surface is a stochastic cut-out to the camera: the pixel is
//   the mean of it and what lies behind, weighed by its opacity.
// - A Cornell box lit by a sphere light and a directional light converges:
//   finite, the same mean whatever the seed or number of light samples, and
//   the noise between seeds falls with the sample count.
// - A scene of a floor, a box, a sphere and a wall under a directional light
//   and a sphere light renders the same image at scales 1e-7, 1e-3, 1 and
//   1e4: the ray offsets follow the scale (no light leaks through contacts
//   at small scales, no acne at large ones, no vanishing sphere lights).

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
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

// Scene //////////////////////////////////////////////////////////////////////

using SetMaterial = std::function<void(anari::Device, anari::Material)>;

anari::Material matte(
    anari::Device d, float3 color, const SetMaterial &set = {})
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  if (set)
    set(d, mat);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geom, anari::Material mat, uint32_t id)
{
  anari::commitParameters(d, geom);
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::setParameter(d, surface, "id", id);
  anari::commitParameters(d, surface);
  return surface;
}

// A quad with corners 'v', counter-clockwise seen from its front
anari::Surface makeQuad(anari::Device d,
    const std::vector<float3> &v,
    anari::Material mat,
    uint32_t id = 0)
{
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  return makeSurface(d, geom, mat, id);
}

// A sphere of 'radius' around the origin
anari::Surface makeSphere(anari::Device d, float radius, anari::Material mat)
{
  const float3 center(0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", radius);
  return makeSurface(d, geom, mat, 0);
}

anari::Light pointLight(anari::Device d,
    float3 position,
    float radius,
    const char *brightness,
    float value)
{
  auto light = anari::newObject<anari::Light>(d, "point");
  anari::setParameter(d, light, "position", position);
  anari::setParameter(d, light, "radius", radius);
  anari::setParameter(d, light, brightness, value);
  anari::commitParameters(d, light);
  return light;
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

anari::World makeWorld(anari::Device d,
    const std::vector<anari::Surface> &surfaces,
    const std::vector<anari::Light> &lights)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto l : lights)
    anari::release(d, l);
  return world;
}

anari::Camera perspective(
    anari::Device d, float3 position, float3 at, float fovy)
{
  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  const float3 dir = normalize(at - position);
  const float3 up =
      std::abs(dir.z) > 0.9f ? float3(0.f, 1.f, 0.f) : float3(0.f, 0.f, 1.f);
  anari::setParameter(d, camera, "position", position);
  anari::setParameter(d, camera, "direction", dir);
  anari::setParameter(d, camera, "up", up);
  anari::setParameter(d, camera, "fovy", fovy);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// Orthographic camera at z = 3 looking down -z at the [-1, 1]^2 square
anari::Camera topCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
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
  std::vector<uint32_t> objectId;
};

// Renders 'world' through 'camera' (released) with a pathtracer of
// 'pixelSamples' paths per pixel set up by 'setParams', 'size' pixels
Image render(anari::Device d,
    anari::World world,
    anari::Camera camera,
    int pixelSamples,
    const SetParams &setParams,
    uint2 size = IMAGE_SIZE)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  anari::setParameter(d, renderer, "pixelSamples", pixelSamples);
  anari::setParameter(d, renderer, "background", float4(0.f, 0.f, 0.f, 1.f));
  if (setParams)
    setParams(d, renderer);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  anari::render(d, frame);
  anari::wait(d, frame);

  Image image;
  {
    auto fb = anari::map<float4>(d, frame, "channel.color");
    if (fb.data && fb.width == size.x && fb.height == size.y)
      image.color.assign(fb.data, fb.data + size_t(size.x) * size.y);
    anari::unmap(d, frame, "channel.color");
  }
  {
    auto fb = anari::map<uint32_t>(d, frame, "channel.objectId");
    if (fb.data && fb.width == size.x && fb.height == size.y)
      image.objectId.assign(fb.data, fb.data + size_t(size.x) * size.y);
    anari::unmap(d, frame, "channel.objectId");
  }
  anari::release(d, frame);
  return image;
}

float3 rgb(float4 c)
{
  return float3(c.x, c.y, c.z);
}

float3 mean(const std::vector<float4> &pixels)
{
  double r = 0.0, g = 0.0, b = 0.0;
  for (const auto &p : pixels) {
    r += p.x;
    g += p.y;
    b += p.z;
  }
  const double n = pixels.empty() ? 1.0 : double(pixels.size());
  return float3(float(r / n), float(g / n), float(b / n));
}

// The largest difference of any pixel's rgb from 'expected'
float maxError(const std::vector<float4> &pixels, float3 expected)
{
  if (pixels.empty())
    return INFINITY;
  float e = 0.f;
  for (const auto &p : pixels)
    e = std::max(e, maxelem(abs(rgb(p) - expected)));
  return e;
}

bool allFiniteNonNegative(const std::vector<float4> &pixels)
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

float rmse(const std::vector<float4> &a, const std::vector<float4> &b)
{
  double sum = 0.0;
  for (size_t i = 0; i < a.size(); i++) {
    const float3 diff = rgb(a[i]) - rgb(b[i]);
    sum += dot(diff, diff);
  }
  return float(std::sqrt(sum / (3.0 * a.size())));
}

bool nearRelative(float3 a, float3 b, float tolerance)
{
  return maxelem(abs(a - b)) <= tolerance * maxelem(abs(b));
}

// Sphere room //////////////////////////////////////////////////////////////

constexpr float ROOM_ALBEDO = 0.5f;

// The inside of a sphere of radius 1 and albedo ROOM_ALBEDO, with 'lights'
anari::World sphereRoom(
    anari::Device d, const std::vector<anari::Light> &lights)
{
  return makeWorld(
      d, {makeSphere(d, 1.f, matte(d, float3(ROOM_ALBEDO)))}, lights);
}

// A point light of intensity pi at the room's center gives every wall point
// the irradiance pi, so after m bounces the wall shows
// a (1 + a + ... + a^m) exactly while no path is played by Russian roulette
bool testBounces(anari::Device d)
{
  bool pass = true;
  auto world =
      sphereRoom(d, {pointLight(d, float3(0.f), 0.f, "intensity", PI)});

  float expected = 0.f;
  for (int m = 0; m <= 2; m++) {
    expected += ROOM_ALBEDO * std::pow(ROOM_ALBEDO, float(m));
    const Image image = render(d,
        world,
        perspective(d, float3(0.f), float3(-1.f, 0.f, 0.f), 1.f),
        1,
        [=](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "maxBounces", m);
        });
    const float e = maxError(image.color, float3(expected));
    pass &= report("point light in a sphere, maxBounces " + std::to_string(m),
        e < 1e-5f,
        "max error " + str(e) + " from " + str(expected));
  }

  // without Russian roulette, 40 bounces: a / (1 - a), but for a^41
  {
    const Image image = render(d,
        world,
        perspective(d, float3(0.f), float3(-1.f, 0.f, 0.f), 1.f),
        1,
        [](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "maxBounces", 40);
          anari::setParameter(d, r, "minBouncesBeforeRR", 41);
        });
    const float e = maxError(image.color, float3(1.f));
    pass &= report("point light in a sphere, 40 bounces without roulette",
        e < 1e-5f,
        "max error " + str(e));
  }

  // Russian roulette is unbiased, from the first bounce or after the
  // default 2 (1% of the mean: 16 samples of 32 x 32 pixels average out
  // roulette's noise to well below that)
  for (int minBounces : {0, 2}) {
    const Image image = render(d,
        world,
        perspective(d, float3(0.f), float3(-1.f, 0.f, 0.f), 1.f),
        16,
        [=](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "maxBounces", 64);
          anari::setParameter(d, r, "minBouncesBeforeRR", minBounces);
        });
    const float3 m = mean(image.color);
    pass &= report("Russian roulette after " + std::to_string(minBounces)
            + " bounces converges to a / (1 - a)",
        allFiniteNonNegative(image.color)
            && nearRelative(m, float3(1.f), 0.01f),
        "mean " + str(m.x));
  }

  anari::release(d, world);
  return pass;
}

// A sphere light of radiance L = 1 / r^2 and radius r at the room's center:
// the wall shows a L r^2 = a with one vertex (light samples and BSDF samples
// that find the light, weighed by MIS)
bool testMis(anari::Device d)
{
  bool pass = true;
  for (float radius : {0.25f, 0.5f}) {
    const float radiance = 1.f / (radius * radius);
    auto world = sphereRoom(
        d, {pointLight(d, float3(0.f), radius, "radiance", radiance)});
    for (int lightSamples : {1, 4}) {
      const Image image = render(d,
          world,
          perspective(d, float3(-0.6f, 0.f, 0.f), float3(-1.f, 0.f, 0.f), 0.5f),
          16,
          [=](anari::Device d, anari::Renderer r) {
            anari::setParameter(d, r, "maxBounces", 0);
            anari::setParameter(d, r, "lightSamplesPerHit", lightSamples);
          });
      // 1% of the mean: the light and BSDF samples' noise averages out over
      // 16 samples of 32 x 32 pixels
      const float3 m = mean(image.color);
      pass &= report("sphere light of radius " + str(radius) + ", "
              + std::to_string(lightSamples) + " light samples (MIS)",
          allFiniteNonNegative(image.color)
              && nearRelative(m, float3(ROOM_ALBEDO), 0.01f),
          "mean " + str(m.x) + " vs " + str(ROOM_ALBEDO));
    }

    // the camera sees the light itself
    const Image image = render(d,
        world,
        perspective(d, float3(-0.9f, 0.f, 0.f), float3(0.f), 0.05f),
        1,
        {});
    const float e = maxError(image.color, float3(radiance));
    pass &= report("camera sees the sphere light of radius " + str(radius),
        e < 1e-5f * radiance,
        "max error " + str(e));
    anari::release(d, world);
  }
  return pass;
}

// Shadows ////////////////////////////////////////////////////////////////////

constexpr uint32_t LIT_FLOOR = 1;
constexpr uint32_t SHADOWED_FLOOR = 2;
constexpr uint32_t BLOCKER = 3;
constexpr float FLOOR_ALBEDO = 0.5f;

// A floor z = 0 of albedo FLOOR_ALBEDO lit straight down with irradiance pi
// (so it shows its albedo where lit): its part x > 0.1 is out of the
// blocker's shadow (object id LIT_FLOOR), its part x < -0.1 in it
// (SHADOWED_FLOOR); the blocker z = 1 over x < 0, of material 'blocker'
anari::World shadowWorld(anari::Device d, anari::Material blocker)
{
  const auto floorMat = [&] { return matte(d, float3(FLOOR_ALBEDO)); };
  return makeWorld(d,
      {makeQuad(d,
           {{0.1f, -2.f, 0.f},
               {2.f, -2.f, 0.f},
               {2.f, 2.f, 0.f},
               {0.1f, 2.f, 0.f}},
           floorMat(),
           LIT_FLOOR),
          makeQuad(d,
              {{-2.f, -2.f, 0.f},
                  {-0.1f, -2.f, 0.f},
                  {-0.1f, 2.f, 0.f},
                  {-2.f, 2.f, 0.f}},
              floorMat(),
              SHADOWED_FLOOR),
          makeQuad(d,
              {{-2.f, -2.f, 1.f},
                  {0.f, -2.f, 1.f},
                  {0.f, 2.f, 1.f},
                  {-2.f, 2.f, 1.f}},
              blocker,
              BLOCKER)},
      {directionalLight(d, float3(0.f, 0.f, -1.f), PI)});
}

// The pixels whose 3x3 neighbourhood sees only object 'id' (away from edges)
std::vector<float4> pixelsOf(const Image &image, uint32_t id)
{
  std::vector<float4> pixels;
  if (image.color.empty() || image.objectId.empty())
    return pixels;
  const int w = int(IMAGE_SIZE.x), h = int(IMAGE_SIZE.y);
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      bool inside = true;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++)
          inside &= image.objectId[size_t(y + dy) * w + x + dx] == id;
      }
      if (inside)
        pixels.push_back(image.color[size_t(y) * w + x]);
    }
  }
  return pixels;
}

// From the side, below the blocker: the camera sees both floors, never
// through the blocker
anari::Camera sideCamera(anari::Device d)
{
  return perspective(d, float3(0.f, -3.f, 0.5f), float3(0.f, 0.f, 0.f), 1.f);
}

bool testShadows(anari::Device d)
{
  bool pass = true;
  struct Case
  {
    std::string name;
    SetMaterial blocker;
    SetParams params;
    float shadowed; // fraction of the light behind the blocker
    float lit; // the lit floor's value
  };
  const SetMaterial opaque = {};
  const auto blend = [](float opacity) -> SetMaterial {
    return [=](anari::Device d, anari::Material m) {
      anari::setParameter(d, m, "opacity", opacity);
      anari::setParameter(d, m, "alphaMode", "blend");
    };
  };
  const auto mask = [](float opacity) -> SetMaterial {
    return [=](anari::Device d, anari::Material m) {
      anari::setParameter(d, m, "opacity", opacity);
      anari::setParameter(d, m, "alphaMode", "mask");
      anari::setParameter(d, m, "alphaCutoff", 0.5f);
    };
  };
  const SetParams noShadows = [](anari::Device d, anari::Renderer r) {
    anari::setParameter(d, r, "disableShadows", true);
  };
  const SetParams clamped = [](anari::Device d, anari::Renderer r) {
    anari::setParameter(d, r, "fireflyClampThreshold", 0.1f);
  };
  const std::vector<Case> cases = {
      {"opaque blocker", opaque, {}, 0.f, FLOOR_ALBEDO},
      {"blend blocker of opacity 0.25", blend(0.25f), {}, 0.75f, FLOOR_ALBEDO},
      {"blend blocker of opacity 0.8", blend(0.8f), {}, 0.2f, FLOOR_ALBEDO},
      {"mask blocker below the cutoff", mask(0.25f), {}, 1.f, FLOOR_ALBEDO},
      {"mask blocker above the cutoff", mask(0.75f), {}, 0.f, FLOOR_ALBEDO},
      {"disableShadows", opaque, noShadows, 1.f, FLOOR_ALBEDO},
      // white luminance: the clamped value in every channel
      {"fireflyClampThreshold 0.1", opaque, clamped, 0.f, 0.1f},
  };

  for (const auto &c : cases) {
    auto world = shadowWorld(d, matte(d, float3(0.7f), c.blocker));
    const Image image = render(
        d, world, sideCamera(d), 1, [&](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "maxBounces", 0);
          if (c.params)
            c.params(d, r);
        });
    const auto lit = pixelsOf(image, LIT_FLOOR);
    const auto shadowed = pixelsOf(image, SHADOWED_FLOOR);
    const float litError = maxError(lit, float3(c.lit));
    const float shadowError =
        maxError(shadowed, float3(std::min(c.lit, FLOOR_ALBEDO * c.shadowed)));
    pass &= report(c.name,
        lit.size() > 20 && shadowed.size() > 20 && litError < 1e-4f
            && shadowError < 1e-4f,
        std::to_string(lit.size()) + " lit pixels off by " + str(litError)
            + ", " + std::to_string(shadowed.size()) + " shadowed off by "
            + str(shadowError));
    anari::release(d, world);
  }
  return pass;
}

// A 'blend' surface of opacity o and albedo b over a floor of albedo f, lit
// straight down with irradiance pi and seen from straight above: o b of the
// pixel is the surface, (1 - o) f (1 - o) the floor in its shadow
bool testPresence(anari::Device d)
{
  bool pass = true;
  const float b = 0.8f;
  for (float o : {0.25f, 0.5f}) {
    auto world = makeWorld(d,
        {makeQuad(d,
             {{-2.f, -2.f, 0.f},
                 {2.f, -2.f, 0.f},
                 {2.f, 2.f, 0.f},
                 {-2.f, 2.f, 0.f}},
             matte(d, float3(FLOOR_ALBEDO))),
            makeQuad(d,
                {{-2.f, -2.f, 1.f},
                    {2.f, -2.f, 1.f},
                    {2.f, 2.f, 1.f},
                    {-2.f, 2.f, 1.f}},
                matte(d,
                    float3(b),
                    [=](anari::Device d, anari::Material m) {
                      anari::setParameter(d, m, "opacity", o);
                      anari::setParameter(d, m, "alphaMode", "blend");
                    }))},
        {directionalLight(d, float3(0.f, 0.f, -1.f), PI)});
    const Image image = render(
        d, world, topCamera(d), 16, [](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "maxBounces", 0);
        });
    // 1% of the mean: each sample sees one of two values, so 16 samples of
    // 32 x 32 pixels average to within about 0.2%
    const float expected = o * b + (1.f - o) * FLOOR_ALBEDO * (1.f - o);
    const float3 m = mean(image.color);
    bool opaque = !image.color.empty();
    for (const auto &p : image.color)
      opaque &= p.w == 1.f;
    pass &= report(
        "blend surface of opacity " + str(o) + " is a stochastic cut-out",
        allFiniteNonNegative(image.color) && opaque
            && nearRelative(m, float3(expected), 0.01f),
        "mean " + str(m.x) + " vs " + str(expected));
    anari::release(d, world);
  }
  return pass;
}

// Cornell box ////////////////////////////////////////////////////////////////

// The box [-1, 1]^3, open at z = 1: white floor, ceiling and back, red left
// and green right walls, a sphere light under the ceiling and a directional
// light shining in through the open side
anari::World cornellBox(anari::Device d)
{
  const auto white = [&] { return matte(d, float3(0.73f)); };
  return makeWorld(d,
      {makeQuad(d,
           {{-1.f, -1.f, 1.f},
               {1.f, -1.f, 1.f},
               {1.f, -1.f, -1.f},
               {-1.f, -1.f, -1.f}},
           white()),
          makeQuad(d,
              {{-1.f, 1.f, -1.f},
                  {1.f, 1.f, -1.f},
                  {1.f, 1.f, 1.f},
                  {-1.f, 1.f, 1.f}},
              white()),
          makeQuad(d,
              {{-1.f, -1.f, -1.f},
                  {1.f, -1.f, -1.f},
                  {1.f, 1.f, -1.f},
                  {-1.f, 1.f, -1.f}},
              white()),
          makeQuad(d,
              {{-1.f, -1.f, 1.f},
                  {-1.f, -1.f, -1.f},
                  {-1.f, 1.f, -1.f},
                  {-1.f, 1.f, 1.f}},
              matte(d, float3(0.65f, 0.05f, 0.05f))),
          makeQuad(d,
              {{1.f, -1.f, -1.f},
                  {1.f, -1.f, 1.f},
                  {1.f, 1.f, 1.f},
                  {1.f, 1.f, -1.f}},
              matte(d, float3(0.12f, 0.45f, 0.15f)))},
      {pointLight(d, float3(0.f, 0.8f, 0.f), 0.15f, "intensity", 2.f),
          directionalLight(d, normalize(float3(-0.3f, -0.5f, -1.f)), 0.5f)});
}

bool testCornellBox(anari::Device d)
{
  bool pass = true;
  auto world = cornellBox(d);
  const uint2 size = {48, 48};
  const auto renderBox = [&](int samples, int seed, int lightSamples) {
    return render(
        d,
        world,
        perspective(d, float3(0.f, 0.f, 3.5f), float3(0.f), 0.7f),
        samples,
        [=](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "randomNumberSeed", seed);
          anari::setParameter(d, r, "lightSamplesPerHit", lightSamples);
        },
        size)
        .color;
  };

  const auto a16 = renderBox(16, 0, 1);
  const auto b16 = renderBox(16, 1, 1);
  const auto a256 = renderBox(256, 0, 1);
  const auto b256 = renderBox(256, 1, 1);
  const auto c64 = renderBox(64, 2, 4);
  pass &= report("Cornell box: finite, non-negative",
      allFiniteNonNegative(a16) && allFiniteNonNegative(b16)
          && allFiniteNonNegative(a256) && allFiniteNonNegative(b256)
          && allFiniteNonNegative(c64));
  if (!pass)
    return pass;

  // Means agree within 2% (5% at 16 samples): the box's indirect light is
  // noisy, and 256 samples of 48 x 48 pixels bring the mean's noise well
  // below that
  const float3 m = mean(a256);
  pass &= report("Cornell box: lit", minelem(m) > 0.02f, "mean " + str(m.x));
  pass &= report("Cornell box: the mean doesn't depend on the seed",
      nearRelative(mean(b256), m, 0.02f),
      str(mean(b256).x) + " vs " + str(m.x));
  pass &= report("Cornell box: the mean doesn't depend on the sample count",
      nearRelative(mean(a16), m, 0.05f),
      str(mean(a16).x) + " vs " + str(m.x));
  pass &= report("Cornell box: nor on the light samples per hit",
      nearRelative(mean(c64), m, 0.02f),
      str(mean(c64).x) + " vs " + str(m.x));
  // independent noise falls as 1 / sqrt(samples): 4 times less at 16 times
  // the samples; 2.5 leaves room for the noise in the noise estimates
  const float noise16 = rmse(a16, b16);
  const float noise256 = rmse(a256, b256);
  pass &= report("Cornell box: noise falls with the sample count",
      noise256 > 0.f && noise16 / noise256 > 2.5f,
      "rmse " + str(noise16) + " at 16 samples, " + str(noise256) + " at 256");

  anari::release(d, world);
  return pass;
}

// Scale /////////////////////////////////////////////////////////////////////

// A box with corners 'lo' and 'hi', its faces outwards
std::vector<anari::Surface> makeBox(
    anari::Device d, float3 lo, float3 hi, float3 color)
{
  const auto corner = [&](int x, int y, int z) {
    return float3(x ? hi.x : lo.x, y ? hi.y : lo.y, z ? hi.z : lo.z);
  };
  const int faces[6][4][3] = {
      {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, // -z
      {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, // +z
      {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, // -y
      {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, // +y
      {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, // -x
      {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, // +x
  };
  std::vector<anari::Surface> surfaces;
  for (const auto &f : faces) {
    std::vector<float3> v;
    for (const auto &c : f)
      v.push_back(corner(c[0], c[1], c[2]));
    surfaces.push_back(makeQuad(d, v, matte(d, color)));
  }
  return surfaces;
}

// A floor with a box and a sphere resting on it and a thin wall standing on
// it, under a directional light and a sphere light of fixed radiance, all
// scaled by 's' about the origin: the light reaching any point doesn't depend
// on 's', so neither does the image through a camera scaled the same way
anari::World scaledScene(anari::Device d, float s)
{
  std::vector<anari::Surface> surfaces = {makeQuad(d,
                                              {{-3.f * s, -3.f * s, 0.f},
                                                  {3.f * s, -3.f * s, 0.f},
                                                  {3.f * s, 3.f * s, 0.f},
                                                  {-3.f * s, 3.f * s, 0.f}},
                                              matte(d, float3(0.8f))),
      makeQuad(d,
          {{-1.5f * s, 1.f * s, 0.f},
              {1.5f * s, 1.f * s, 0.f},
              {1.5f * s, 1.f * s, 1.2f * s},
              {-1.5f * s, 1.f * s, 1.2f * s}},
          matte(d, float3(0.6f, 0.7f, 0.5f)))};
  for (auto box : makeBox(d,
           float3(-1.f, -0.4f, 0.f) * s,
           float3(-0.2f, 0.4f, 0.8f) * s,
           float3(0.7f, 0.3f, 0.2f)))
    surfaces.push_back(box);

  const float3 center = float3(0.6f, -0.2f, 0.4f) * s;
  auto sphere = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, sphere, "vertex.position", &center, 1);
  anari::setParameter(d, sphere, "radius", 0.4f * s);
  surfaces.push_back(
      makeSurface(d, sphere, matte(d, float3(0.3f, 0.4f, 0.8f)), 0));

  return makeWorld(d,
      surfaces,
      {directionalLight(d, normalize(float3(0.5f, 0.7f, -0.6f)), 2.f),
          pointLight(
              d, float3(1.4f, -1.2f, 1.6f) * s, 0.2f * s, "radiance", 10.f)});
}

// The same scene at scales 1e-7, 1e-3, 1 and 1e4 renders the same image
// within the noise between seeds: the ray offsets follow the scale, so small
// scenes don't leak light through contacts and large ones show no acne, and
// sphere lights don't vanish in tiny scenes (Typhoon's fixed 1e-6 light
// intersection tolerance drops the sphere light at 1e-7)
bool testScale(anari::Device d)
{
  bool pass = true;
  const uint2 size = {48, 48};
  const auto renderScaled = [&](float s, int seed) {
    auto world = scaledScene(d, s);
    auto image = render(
        d,
        world,
        perspective(
            d, float3(0.3f, -4.f, 2.5f) * s, float3(0.f, 0.f, 0.4f) * s, 0.8f),
        16,
        [=](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "randomNumberSeed", seed);
          anari::setParameter(d, r, "maxBounces", 3);
        },
        size)
                     .color;
    anari::release(d, world);
    return image;
  };

  const auto reference = renderScaled(1.f, 0);
  const float noise = rmse(reference, renderScaled(1.f, 1));
  const float3 m = mean(reference);
  pass &= report("scaled scene: lit at scale 1",
      allFiniteNonNegative(reference) && minelem(m) > 0.05f,
      "mean " + str(m.x));
  for (float s : {1e-7f, 1e-3f, 1e4f}) {
    const auto image = renderScaled(s, 0);
    // the same samples at another scale: rounding moves few paths, so the
    // difference is a tenth of the noise between seeds or less (fixed
    // offsets of 1e-4 differ by 2 and 5 times that noise at 1e-3 and 1e4,
    // a fixed 1e-6 light tolerance by 20 times at 1e-7), and the mean
    // agrees within 0.5%
    const float e = rmse(image, reference);
    pass &= report("scaled scene: scale " + str(s) + " renders as scale 1",
        allFiniteNonNegative(image) && e < 0.1f * noise
            && nearRelative(mean(image), m, 0.005f),
        "rmse " + str(e) + " (noise between seeds " + str(noise) + "), mean "
            + str(mean(image).x) + " vs " + str(m.x));
  }
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

  bool pass = testBounces(d);
  pass &= testMis(d);
  pass &= testShadows(d);
  pass &= testPresence(d);
  pass &= testCornellBox(d);
  pass &= testScale(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors or warnings", g_errors == 0);
  return pass ? 0 : 1;
}
