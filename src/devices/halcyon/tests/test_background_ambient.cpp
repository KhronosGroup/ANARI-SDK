// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality''s background and ambient light:
// - the background (KHR_RENDERER_BACKGROUND_COLOR/_IMAGE) is what camera
//   rays that miss see, with its alpha, and lights nothing;
// - the ambient light (KHR_RENDERER_AMBIENT_LIGHT, 'ambientColor' x
//   'ambientRadiance') is a constant dome that camera rays don't see and
//   that lights the scene, sampled and gathered like an hdri light: a white
//   Lambertian sphere and a white scattering volume in it render to its
//   radiance (furnace tests), a Lambertian floor to its albedo times it at
//   every 'maxBounces' and light sample count, a surface without a material
//   (lit only by next-event estimation) to the invalid material colour times
//   it, and negative values are clamped to 0;
// - with hdri lights: camera misses see the sum of the 'visible' hdris,
//   opaque, else the background, never the ambient light, and the ambient
//   light and every hdri, visible or not, light the scene together.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr float PI = 3.14159265358979323846f;
constexpr uint2 IMAGE_SIZE = {32, 32};
const float3 ALBEDO = {0.8f, 0.6f, 0.4f};
const float3 AMBIENT_COLOR = {1.f, 0.5f, 0.75f};
constexpr float AMBIENT_RADIANCE = 1.5f;
const float3 AMBIENT = AMBIENT_COLOR * AMBIENT_RADIANCE;
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 0.5f};
// the device's default 'invalidMaterialColor'
const float3 INVALID_MATERIAL_COLOR = {1.f, 0.f, 1.f};

int g_errors = 0;
std::vector<std::string> g_warnings;

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
    g_warnings.push_back(message);
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

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geom, float3 color, bool material = true)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  if (material) {
    auto mat = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, mat, "color", color);
    anari::commitParameters(d, mat);
    anari::setAndReleaseParameter(d, surface, "material", mat);
  }
  anari::commitParameters(d, surface);
  return surface;
}

// The floor z = 0 over x in [-1, 0], y in [-1, 1] (the left half of the
// frame), facing +z; without a material if not 'material'
anari::Surface makeFloor(
    anari::Device d, float3 color = ALBEDO, bool material = true)
{
  const std::vector<float3> v = {
      {-1.f, -1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return makeSurface(d, geom, color, material);
}

// A white unit sphere at the origin
anari::Surface makeSphere(anari::Device d)
{
  const float3 center(0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", 1.f);
  anari::commitParameters(d, geom);
  return makeSurface(d, geom, float3(1.f));
}

// A box [-1, 1]^3 of white (non-absorbing) scattering medium of 'opacity'
anari::Volume makeWhiteVolume(anari::Device d, float opacity)
{
  const std::vector<float> voxels(8, 1.f);
  auto f = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, f, "data", voxels.data(), 2, 2, 2);
  anari::setParameter(d, f, "origin", float3(-1.f));
  anari::setParameter(d, f, "spacing", float3(2.f));
  anari::commitParameters(d, f);
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", f);
  anari::setParameter(d, vol, "color", float3(1.f));
  anari::setParameter(d, vol, "opacity", opacity);
  anari::setParameter(d, vol, "unitDistance", 1.f);
  anari::commitParameters(d, vol);
  return vol;
}

// An hdri light of one texel of radiance 'L', 'visible' or not: a uniform
// dome
anari::Light makeUniformHdri(anari::Device d, float3 L, bool visible)
{
  auto light = anari::newObject<anari::Light>(d, "hdri");
  anari::setParameterArray2D(d, light, "radiance", &L, 1, 1);
  anari::setParameter(d, light, "visible", visible);
  anari::commitParameters(d, light);
  return light;
}

// A world of 'surfaces', 'volumes' and 'lights' (all released)
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Light> lights = {},
    std::vector<anari::Volume> volumes = {})
{
  auto world = anari::newObject<anari::World>(d);
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
  if (!volumes.empty())
    anari::setParameterArray1D(
        d, world, "volume", volumes.data(), volumes.size());
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto v : volumes)
    anari::release(d, v);
  for (auto l : lights)
    anari::release(d, l);
  return world;
}

struct View
{
  const char *type{"orthographic"};
  float3 position{0.f, 0.f, 3.f};
  float3 direction{0.f, 0.f, -1.f};
  float3 up{0.f, 1.f, 0.f};
};

using SetParams = std::function<void(anari::Device, anari::Renderer)>;

struct Settings
{
  int pixelSamples{64};
  int maxBounces{16};
  int lightSamplesPerHit{1};
  // the ambient light, if 'ambient'
  bool ambient{true};
  float3 ambientColor{AMBIENT_COLOR};
  float ambientRadiance{AMBIENT_RADIANCE};
  // the background colour, unless set otherwise here
  SetParams background;
};

// The colour channel of 'world' through the pathtracer
std::vector<float4> render(anari::Device d,
    anari::World world,
    const Settings &s = {},
    const View &view = {})
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  if (s.background)
    s.background(d, renderer);
  else
    anari::setParameter(d, renderer, "background", BACKGROUND);
  if (s.ambient) {
    anari::setParameter(d, renderer, "ambientColor", s.ambientColor);
    anari::setParameter(d, renderer, "ambientRadiance", s.ambientRadiance);
  }
  anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", s.maxBounces);
  anari::setParameter(d, renderer, "lightSamplesPerHit", s.lightSamplesPerHit);
  anari::setParameter(d, renderer, "fireflyClampThreshold", 0.f);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, view.type);
  anari::setParameter(d, camera, "position", view.position);
  anari::setParameter(d, camera, "direction", view.direction);
  anari::setParameter(d, camera, "up", view.up);
  if (std::string(view.type) == "orthographic")
    anari::setParameter(d, camera, "height", 2.f);
  else
    anari::setParameter(d, camera, "fovy", 0.6f);
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

float4 pixel(const std::vector<float4> &image, uint32_t x, uint32_t y)
{
  return image[size_t(y) * IMAGE_SIZE.x + x];
}

// Whether pixel column x lies on the floor of makeFloor() (x < 0), away
// from its edge
bool onFloor(uint32_t x)
{
  return x < IMAGE_SIZE.x / 2 - 1;
}

// Whether pixel column x misses makeFloor(), away from its edge
bool offFloor(uint32_t x)
{
  return x > IMAGE_SIZE.x / 2;
}

// The image's pixels on the floor match 'expected': their mean within
// 'meanTolerance' and each within 'pixelTolerance' (relative, per channel;
// absolute where 'expected' is 0), opaque
bool floorMatches(const std::string &name,
    const std::vector<float4> &image,
    float3 expected,
    float pixelTolerance = 0.1f,
    float meanTolerance = 0.005f)
{
  if (image.empty())
    return report(name, false);
  double sum[3] = {};
  float worst = 0.f;
  bool opaque = true;
  size_t n = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      if (!onFloor(x))
        continue;
      const float4 p = pixel(image, x, y);
      for (int c = 0; c < 3; c++) {
        sum[c] += p[c];
        const float error = std::abs(p[c] - expected[c]);
        worst =
            std::max(worst, expected[c] > 0.f ? error / expected[c] : error);
      }
      opaque &= p.w == 1.f;
      n++;
    }
  }
  const float3 mean =
      float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(n);
  bool meanOk = true;
  for (int c = 0; c < 3; c++) {
    meanOk &= std::abs(mean[c] - expected[c])
        <= meanTolerance * std::max(expected[c], 1e-3f);
  }
  const bool ok = meanOk && worst <= pixelTolerance && opaque;
  if (!ok) {
    std::printf("  mean %s, expected %s, worst pixel error %g%s\n",
        str(mean).c_str(),
        str(expected).c_str(),
        worst,
        opaque ? "" : ", not opaque");
  }
  return report(name, ok);
}

// Every pixel off the floor is 'expected' exactly
bool missesShow(
    const std::string &name, const std::vector<float4> &image, float4 expected)
{
  bool ok = !image.empty();
  for (uint32_t y = 0; ok && y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; ok && x < IMAGE_SIZE.x; x++) {
      if (!offFloor(x))
        continue;
      const float4 p = pixel(image, x, y);
      ok &= p.x == expected.x && p.y == expected.y && p.z == expected.z
          && p.w == expected.w;
      if (!ok) {
        std::printf(
            "  pixel (%u, %u) is (%g, %g, %g, %g)\n", x, y, p.x, p.y, p.z, p.w);
      }
    }
  }
  return report(name, ok);
}

// Tests //////////////////////////////////////////////////////////////////////

// The background colour and image are what camera rays that miss see, with
// their alpha; they light nothing (without lights or an ambient light the
// floor is black)
bool testBackground(anari::Device d)
{
  bool pass = true;
  auto world = makeWorld(d, {makeFloor(d)});

  Settings s;
  s.ambient = false;
  s.pixelSamples = 4;
  auto image = render(d, world, s);
  pass &= missesShow("misses show the background colour", image, BACKGROUND);
  pass &= floorMatches(
      "the background colour lights nothing", image, float3(0.f), 0.f, 0.f);

  // an image the frame's size, in vertical bands 8 texels wide, so that the
  // jittered camera rays of a pixel inside a band see its colour exactly
  // (the image is interpolated between texel centres)
  std::vector<float4> texels(size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y);
  const auto bandColor = [](uint32_t x) {
    const float b = float(x / 8);
    return float4(0.2f + 0.1f * b, 0.9f - 0.2f * b, 0.5f, 0.25f + 0.2f * b);
  };
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++)
      texels[size_t(y) * IMAGE_SIZE.x + x] = bandColor(x);
  }
  s.background = [&](anari::Device d, anari::Renderer r) {
    anari::setParameterArray2D(
        d, r, "background", texels.data(), IMAGE_SIZE.x, IMAGE_SIZE.y);
  };
  image = render(d, world, s);
  bool imageShown = !image.empty();
  for (uint32_t y = 0; imageShown && y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      if (!offFloor(x) || x % 8 == 0 || x % 8 == 7)
        continue;
      const float4 p = pixel(image, x, y);
      const float4 e = bandColor(x);
      imageShown &= p.x == e.x && p.y == e.y && p.z == e.z && p.w == e.w;
    }
  }
  pass &=
      report("misses show the background image, with its alpha", imageShown);
  pass &= floorMatches(
      "the background image lights nothing", image, float3(0.f), 0.f, 0.f);

  anari::release(d, world);
  return pass;
}

// The ambient light lights the scene as a constant dome of its radiance;
// camera rays don't see it
bool testAmbient(anari::Device d)
{
  bool pass = true;

  // The furnace tests' background is the ambient radiance, opaque, so that
  // camera rays that miss (or pass a medium) see what the light is
  const SetParams ambientBackground = [](anari::Device d, anari::Renderer r) {
    anari::setParameter(d, r, "background", float4(AMBIENT, 1.f));
  };

  // furnace: a white Lambertian sphere in it renders to its radiance
  {
    auto world = makeWorld(d, {makeSphere(d)});
    View view;
    view.type = "perspective";
    view.position = float3(0.f, -4.f, 0.5f);
    view.direction = normalize(float3(0.f, 4.f, -0.5f));
    view.up = float3(0.f, 0.f, 1.f);
    for (int lightSamples : {1, 4}) {
      Settings s;
      s.pixelSamples = 256 / lightSamples;
      s.lightSamplesPerHit = lightSamples;
      s.background = ambientBackground;
      const auto image = render(d, world, s, view);
      double sum[3] = {};
      float worst = 0.f;
      bool opaque = !image.empty();
      for (const auto &p : image) {
        for (int c = 0; c < 3; c++) {
          sum[c] += p[c];
          worst = std::max(worst, std::abs(p[c] - AMBIENT[c]) / AMBIENT[c]);
        }
        opaque &= p.w == 1.f;
      }
      const float3 mean = image.empty()
          ? float3(0.f)
          : float3(float(sum[0]), float(sum[1]), float(sum[2]))
              / float(image.size());
      bool meanOk = true;
      for (int c = 0; c < 3; c++)
        meanOk &= std::abs(mean[c] - AMBIENT[c]) <= 0.003f * AMBIENT[c];
      const std::string suffix =
          " (lightSamplesPerHit " + std::to_string(lightSamples) + ")";
      // (several light samples are noisier per pixel, as for the floor)
      const float pixelTolerance = lightSamples > 1 ? 0.15f : 0.1f;
      if (!meanOk || worst > pixelTolerance) {
        std::printf("  mean %s, expected %s, worst pixel error %g\n",
            str(mean).c_str(),
            str(AMBIENT).c_str(),
            worst);
      }
      pass &= report(
          "furnace: the image's mean is the ambient radiance" + suffix, meanOk);
      pass &=
          report("furnace: every pixel is near the ambient radiance" + suffix,
              worst <= pixelTolerance);
      pass &= report("furnace: the image is opaque" + suffix, opaque);
    }
    anari::release(d, world);
  }

  // a white scattering medium in it renders to its radiance
  {
    auto world = makeWorld(d, {}, {}, {makeWhiteVolume(d, 0.5f)});
    Settings s;
    s.maxBounces = 256;
    s.background = ambientBackground;
    View view;
    view.position = float3(0.3f, -3.f, 0.2f);
    view.direction = float3(0.f, 1.f, 0.f);
    view.up = float3(0.f, 0.f, 1.f);
    const auto image = render(d, world, s, view);
    double sum[3] = {};
    for (const auto &p : image) {
      for (int c = 0; c < 3; c++)
        sum[c] += p[c];
    }
    const float3 mean = image.empty()
        ? float3(0.f)
        : float3(float(sum[0]), float(sum[1]), float(sum[2]))
            / float(image.size());
    bool ok = !image.empty();
    for (int c = 0; c < 3; c++)
      ok &= std::abs(mean[c] - AMBIENT[c]) <= 0.01f * AMBIENT[c];
    if (!ok)
      std::printf(
          "  mean %s, expected %s\n", str(mean).c_str(), str(AMBIENT).c_str());
    pass &= report(
        "volume furnace: a white medium renders to the ambient radiance", ok);
    anari::release(d, world);
  }

  // a lone Lambertian floor: its albedo times the radiance, at every
  // 'maxBounces' and light sample count; misses show the background, not
  // the ambient light
  {
    auto world = makeWorld(d, {makeFloor(d)});
    for (int maxBounces : {0, 16}) {
      for (int lightSamples : {1, 4}) {
        Settings s;
        s.maxBounces = maxBounces;
        s.lightSamplesPerHit = lightSamples;
        s.pixelSamples = 64 / lightSamples;
        const std::string suffix = " (maxBounces " + std::to_string(maxBounces)
            + ", lightSamplesPerHit " + std::to_string(lightSamples) + ")";
        const auto image = render(d, world, s);
        // (several light samples are less evenly spread over a pixel's
        // samples than one, so the pixels are noisier)
        pass &= floorMatches("floor is albedo * ambient" + suffix,
            image,
            ALBEDO * AMBIENT,
            lightSamples > 1 ? 0.15f : 0.1f);
        pass &= missesShow("camera rays don't see the ambient light" + suffix,
            image,
            BACKGROUND);
      }
    }
    anari::release(d, world);
  }

  // a surface without a material is lit, by next-event estimation only, as
  // a Lambertian of the invalid material colour: also by the ambient light
  {
    auto world = makeWorld(d, {makeFloor(d, ALBEDO, false)});
    const auto image = render(d, world);
    pass &= floorMatches("no material: the invalid material colour * ambient",
        image,
        INVALID_MATERIAL_COLOR * AMBIENT,
        0.15f);
    anari::release(d, world);
  }

  // negative values are clamped to 0: no light (and no NaNs)
  {
    auto world = makeWorld(d, {makeFloor(d)});
    Settings s;
    s.pixelSamples = 4;
    s.ambientRadiance = -1.f;
    auto image = render(d, world, s);
    pass &= floorMatches("a negative ambientRadiance lights nothing",
        image,
        float3(0.f),
        0.f,
        0.f);
    s.ambientRadiance = AMBIENT_RADIANCE;
    s.ambientColor = float3(-1.f, 0.5f, 1.f);
    s.pixelSamples = 64;
    image = render(d, world, s);
    pass &= floorMatches("a negative ambientColor component is 0",
        image,
        ALBEDO * float3(0.f, 0.5f, 1.f) * AMBIENT_RADIANCE);
    anari::release(d, world);
  }

  return pass;
}

// With hdri lights: camera misses see the visible hdris (opaque) or the
// background, never the ambient light; the ambient light and the hdris,
// visible or not, light the floor together
bool testAmbientAndHdri(anari::Device d)
{
  bool pass = true;
  const float3 L = float3(0.4f, 0.8f, 0.2f);
  // a Lambertian floor under uniform domes of total radiance R shows
  // albedo * R
  const float3 lit = ALBEDO * (AMBIENT + L);

  auto world = makeWorld(d, {makeFloor(d)}, {makeUniformHdri(d, L, true)});
  auto image = render(d, world);
  pass &= missesShow("a visible hdri shows, opaque, without the ambient light",
      image,
      float4(L, 1.f));
  pass &=
      floorMatches("ambient and a visible hdri light the floor", image, lit);
  anari::release(d, world);

  world = makeWorld(d, {makeFloor(d)}, {makeUniformHdri(d, L, false)});
  image = render(d, world);
  pass &=
      missesShow("an invisible hdri shows the background", image, BACKGROUND);
  pass &=
      floorMatches("ambient and an invisible hdri light the floor", image, lit);
  anari::release(d, world);

  // two hdris, one visible: the visible one shows, all three light
  const float3 L2 = float3(0.3f, 0.1f, 0.6f);
  world = makeWorld(d,
      {makeFloor(d)},
      {makeUniformHdri(d, L, false), makeUniformHdri(d, L2, true)});
  image = render(d, world);
  pass &= missesShow("only the visible hdri shows", image, float4(L2, 1.f));
  pass &= floorMatches("ambient and both hdris light the floor",
      image,
      ALBEDO * (AMBIENT + L + L2));
  anari::release(d, world);

  return pass;
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = true;
  pass &= testBackground(d);
  pass &= testAmbient(d);
  pass &= testAmbientAndHdri(d);
  pass &= report("no errors", g_errors == 0);
  pass &= report("no warnings but the missing material's",
      std::all_of(
          g_warnings.begin(), g_warnings.end(), [](const std::string &w) {
            return w.find("missing 'material'") != std::string::npos;
          }));

  anari::release(d, d);
  anari::unloadLibrary(library);
  return pass ? 0 : 1;
}
