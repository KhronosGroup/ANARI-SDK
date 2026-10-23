// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality' renderer skeleton: introspection, a Lambertian (matte) floor
// under the ambient light (on a lone floor, where every bounce escapes, its
// color times the light; test_background_ambient.cpp checks the ambient light
// and the background in depth), the background, 'maxBounces', 'pixelSamples',
// progressive accumulation (KHR_FRAME_ACCUMULATION) and its reset, convergence,
// and depth and id channels matching the 'preview' renderer's.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float3 FLOOR_COLOR = {0.8f, 0.5f, 0.25f};
const float3 AMBIENT_COLOR = {1.f, 0.5f, 0.25f};
constexpr float AMBIENT_RADIANCE = 2.f;
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 0.5f};

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

bool report(const char *name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
  return ok;
}

// Scene /////////////////////////////////////////////////////////////////////

using SetMaterial = std::function<void(anari::Device, anari::Surface)>;

// A matte material of 'color' on 'surface'
SetMaterial matte(float3 color)
{
  return [=](anari::Device d, anari::Surface surface) {
    auto mat = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, mat, "color", color);
    anari::commitParameters(d, mat);
    anari::setAndReleaseParameter(d, surface, "material", mat);
  };
}

// A quad with corners 'v' (counter-clockwise seen from its front), 'id' as its
// surface id, its material set by 'setMaterial' (none if empty)
anari::Surface makeQuad(anari::Device d,
    const std::vector<float3> &v,
    uint32_t id,
    const SetMaterial &setMaterial,
    const std::function<void(anari::Device, anari::Geometry)> &setGeometry =
        {})
{
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  if (setGeometry)
    setGeometry(d, geom);
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "id", id);
  if (setMaterial)
    setMaterial(d, surface);
  anari::commitParameters(d, surface);
  return surface;
}

anari::World makeWorld(anari::Device d, std::vector<anari::Surface> surfaces)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  return world;
}

// The floor z = 0 over x in [-1, xMax], y in [-1, 1], facing the camera
std::vector<float3> floorCorners(float xMax)
{
  return {{-1.f, -1.f, 0.f}, {xMax, -1.f, 0.f}, {xMax, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
}

// The floor over the left half of the frame, alone: every ray leaving it
// escapes, so it shows its color times the ambient light
anari::World makeFloorWorld(anari::Device d, const SetMaterial &setMaterial)
{
  return makeWorld(d, {makeQuad(d, floorCorners(0.f), 7, setMaterial)});
}

// The floor over x < 0.5 and a wall x = 0.5 rising from it to z = 2, edge-on
// to the camera: the wall hides part of the floor's sky and lights it by
// interreflection, so floor pixels are noisy and brighten with bounces
anari::World makeCornerWorld(anari::Device d)
{
  const std::vector<float3> wall = {
      {0.5f, -1.f, 0.f}, {0.5f, 1.f, 0.f}, {0.5f, 1.f, 2.f}, {0.5f, -1.f, 2.f}};
  return makeWorld(d,
      {makeQuad(d, floorCorners(0.5f), 1, matte(FLOOR_COLOR)),
          makeQuad(d, wall, 2, matte(float3(0.9f)))});
}

// Orthographic camera at z = 3 looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
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

// The pathtracer lit by the test's ambient light, over the test's background
void litParams(anari::Device d, anari::Renderer r)
{
  anari::setParameter(d, r, "ambientColor", AMBIENT_COLOR);
  anari::setParameter(d, r, "ambientRadiance", AMBIENT_RADIANCE);
  anari::setParameter(d, r, "background", BACKGROUND);
}

SetParams withSamples(int pixelSamples, int maxBounces = 16)
{
  return [=](anari::Device d, anari::Renderer r) {
    litParams(d, r);
    anari::setParameter(d, r, "pixelSamples", pixelSamples);
    anari::setParameter(d, r, "maxBounces", maxBounces);
  };
}

// A frame of 'world' through makeCamera() with a renderer of 'subtype' set up
// by 'setParams', a float color channel, and depth and id channels if
// 'channels'
struct Frame
{
  anari::Device d;
  anari::Frame frame;
  anari::Camera camera;

  Frame(anari::Device d,
      anari::World world,
      const char *subtype,
      const SetParams &setParams,
      bool accumulation,
      bool channels = false)
      : d(d)
  {
    auto renderer = anari::newObject<anari::Renderer>(d, subtype);
    // one render is the whole first render (no progressive resolution)
    anari::setParameter(d, renderer, "resolutionDivider", 0);
    if (setParams)
      setParams(d, renderer);
    anari::commitParameters(d, renderer);

    camera = makeCamera(d);
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    if (channels) {
      anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
      anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
      anari::setParameter(d, frame, "channel.objectId", ANARI_UINT32);
    }
    anari::setParameter(d, frame, "accumulation", accumulation);
    anari::setParameter(d, frame, "camera", camera);
    anari::setAndReleaseParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Frame()
  {
    anari::release(d, frame);
    anari::release(d, camera);
  }

  // Renders 'n' times, returning the color channel after the last
  std::vector<float4> render(int n = 1)
  {
    for (int i = 0; i < n; i++) {
      anari::render(d, frame);
      anari::wait(d, frame);
    }
    return channel<float4>("channel.color");
  }

  template <typename T>
  std::vector<T> channel(const char *name)
  {
    std::vector<T> pixels;
    auto fb = anari::map<T>(d, frame, name);
    if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
      pixels.assign(fb.data, fb.data + fb.width * fb.height);
    else
      std::printf("FAIL map %s\n", name);
    anari::unmap(d, frame, name);
    return pixels;
  }
};

size_t pixelIndex(float2 pos)
{
  const auto x = uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x);
  const auto y = uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y);
  return size_t(y) * IMAGE_SIZE.x + x;
}

bool near(float4 a, float4 b, float tolerance)
{
  const float4 d = linalg::abs(a - b);
  return d.x <= tolerance && d.y <= tolerance && d.z <= tolerance
      && d.w <= tolerance;
}

bool sameImage(const std::vector<float4> &a,
    const std::vector<float4> &b,
    float tolerance)
{
  if (a.size() != b.size() || a.empty())
    return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (!near(a[i], b[i], tolerance))
      return false;
  }
  return true;
}

// Root mean square difference of the color channels of the pixels of 'a'
// and 'b' with x below 'xMax' (in [-1, 1])
float rmse(const std::vector<float4> &a, const std::vector<float4> &b, float xMax)
{
  double sum = 0.0;
  size_t n = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      if ((x + 0.5f) / IMAGE_SIZE.x * 2.f - 1.f >= xMax)
        continue;
      const size_t i = size_t(y) * IMAGE_SIZE.x + x;
      const float3 diff = float3(a[i].x, a[i].y, a[i].z)
          - float3(b[i].x, b[i].y, b[i].z);
      sum += linalg::dot(diff, diff);
      n++;
    }
  }
  return n ? float(std::sqrt(sum / (3.0 * n))) : 0.f;
}

// Mean of the red channel over the pixels with x in [xMin, xMax)
float meanRed(const std::vector<float4> &a, float xMin, float xMax)
{
  double sum = 0.0;
  size_t n = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float px = (x + 0.5f) / IMAGE_SIZE.x * 2.f - 1.f;
      if (px < xMin || px >= xMax)
        continue;
      sum += a[size_t(y) * IMAGE_SIZE.x + x].x;
      n++;
    }
  }
  return n ? float(sum / n) : 0.f;
}

const float2 FLOOR_POS = {-0.5f, 0.f};
const float2 MISS_POS = {0.5f, 0.f};

// Tests /////////////////////////////////////////////////////////////////////

// 'quality' is a renderer subtype with the parameters it honours, and the
// device declares KHR_FRAME_ACCUMULATION
bool testIntrospection(anari::Library library, anari::Device d)
{
  bool pass = true;

  bool listed = false;
  if (auto *subtypes = anariGetObjectSubtypes(d, ANARI_RENDERER)) {
    for (int i = 0; subtypes[i]; i++)
      listed |= std::strcmp(subtypes[i], "quality") == 0;
  }
  pass &= report("pathtracer listed as a renderer subtype", listed);

  auto *maxBounces = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "maxBounces",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  pass &= report(
      "maxBounces declared, default 16", maxBounces && *maxBounces == 16);

  auto *pixelSamples = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "pixelSamples",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  pass &= report(
      "pixelSamples declared, default 1", pixelSamples && *pixelSamples == 1);

  auto *seed = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "randomNumberSeed",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  pass &= report("randomNumberSeed declared, default -1", seed && *seed == -1);

  // Typhoon's integrator settings
  auto *lightSamples = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "lightSamplesPerHit",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  pass &= report("lightSamplesPerHit declared, default 1",
      lightSamples && *lightSamples == 1);
  auto *rr = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "minBouncesBeforeRR",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  pass &= report("minBouncesBeforeRR declared, default 2", rr && *rr == 2);
  auto *clampThreshold = (const float *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "fireflyClampThreshold",
      ANARI_FLOAT32,
      "default",
      ANARI_FLOAT32);
  pass &= report("fireflyClampThreshold declared, default 20",
      clampThreshold && *clampThreshold == 20.f);
  auto *noShadows = (const bool *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "disableShadows",
      ANARI_BOOL,
      "default",
      ANARI_BOOL);
  pass &= report(
      "disableShadows declared, default false", noShadows && !*noShadows);

  // Typhoon's caustics settings, with its defaults
  auto *caustics = (const bool *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "enableCaustics",
      ANARI_BOOL,
      "default",
      ANARI_BOOL);
  pass &= report(
      "enableCaustics declared, default false", caustics && !*caustics);
  auto *causticsClamp = (const float *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "quality",
      "causticsClampThreshold",
      ANARI_FLOAT32,
      "default",
      ANARI_FLOAT32);
  pass &= report("causticsClampThreshold declared, default 5",
      causticsClamp && *causticsClamp == 5.f);

  bool accumulation = false;
  if (auto *extensions = anariGetDeviceExtensions(library, "default")) {
    for (int i = 0; extensions[i]; i++) {
      accumulation |=
          std::strcmp(extensions[i], "ANARI_KHR_FRAME_ACCUMULATION") == 0;
    }
  }
  pass &= report("KHR_FRAME_ACCUMULATION declared", accumulation);

  return pass;
}

// A lone floor shows its color times the ambient light (every bounce
// escapes; within the noise of sampling the ambient light's dome, which MIS
// weighs against BSDF sampling), whatever 'maxBounces'; misses show the
// background
bool testLambertUnderAmbient(anari::Device d)
{
  bool pass = true;
  auto world = makeFloorWorld(d, matte(FLOOR_COLOR));
  const float3 expected = FLOOR_COLOR * AMBIENT_COLOR * AMBIENT_RADIANCE;

  for (int maxBounces : {0, 16}) {
    Frame f(d, world, "quality", withSamples(64, maxBounces), false);
    const auto pixels = f.render();
    if (pixels.empty())
      return false;
    const std::string what = "maxBounces " + std::to_string(maxBounces);

    bool floorNear = true;
    bool missBackground = true;
    double sum[3] = {};
    size_t n = 0;
    for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
      for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
        const float4 c = pixels[size_t(y) * IMAGE_SIZE.x + x];
        // pixels that the floor's edge x = 0 crosses are a mix
        if (x < IMAGE_SIZE.x / 2) {
          for (int i = 0; i < 3; i++) {
            floorNear &= std::abs(c[i] - expected[i]) <= 0.1f * expected[i];
            sum[i] += c[i];
          }
          floorNear &= c.w == 1.f;
          n++;
        } else if (x > IMAGE_SIZE.x / 2) {
          missBackground &= near(c, BACKGROUND, 0.f);
        }
      }
    }
    // 64 samples leave a few percent of noise per pixel; the floor's mean
    // over hundreds of pixels is within a fraction of a percent
    bool meanNear = n > 0;
    for (int i = 0; i < 3; i++)
      meanNear &= std::abs(sum[i] / n - expected[i]) <= 0.005f * expected[i];
    pass &= report(("floor is color * ambient, " + what).c_str(),
        floorNear && meanNear);
    pass &= report(
        ("misses show the background, " + what).c_str(), missBackground);
  }

  // khr_renderer_ambient_light's default radiance is 0: no light at all
  {
    Frame f(d,
        world,
        "quality",
        [](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "background", BACKGROUND);
        },
        false);
    const auto pixels = f.render();
    pass &= report("ambientRadiance defaults to 0 (black floor)",
        !pixels.empty()
            && near(pixels[pixelIndex(FLOOR_POS)], float4(0.f, 0.f, 0.f, 1.f), 0.f));
  }

  anari::release(d, world);
  return pass;
}

// The Lambertian's color is the material's color at the hit (here from a
// vertex color attribute, which renders as the same constant color does),
// and a surface without a material gets the device's invalid material color
bool testSurfaceColor(anari::Device d)
{
  bool pass = true;

  const float3 vertexColor = {0.2f, 0.6f, 0.4f};
  auto attributeWorld = makeWorld(d,
      {makeQuad(
          d,
          floorCorners(0.f),
          1,
          [](anari::Device d, anari::Surface surface) {
            auto mat = anari::newObject<anari::Material>(d, "matte");
            anari::setParameter(d, mat, "color", "color");
            anari::commitParameters(d, mat);
            anari::setAndReleaseParameter(d, surface, "material", mat);
          },
          [&](anari::Device d, anari::Geometry g) {
            const std::vector<float3> colors(4, vertexColor);
            anari::setParameterArray1D(
                d, g, "vertex.color", colors.data(), colors.size());
          })});
  auto constantWorld = makeFloorWorld(d, matte(vertexColor));
  {
    Frame f(d, attributeWorld, "quality", withSamples(4), false);
    Frame g(d, constantWorld, "quality", withSamples(4), false);
    const auto pixels = f.render();
    const auto constant = g.render();
    pass &= report("color from a vertex attribute",
        // (within the rounding of interpolating the vertex colors)
        sameImage(pixels, constant, 1e-5f));
  }
  anari::release(d, attributeWorld);
  anari::release(d, constantWorld);

  // A surface without a material is lit as a Lambertian of the device's
  // invalid material color, by next-event estimation only: as in Typhoon,
  // its path goes no further (test_background_ambient.cpp has it under the
  // ambient light, which next-event estimation samples too)
  auto invalidWorld = makeFloorWorld(d, {});
  {
    auto light = anari::newObject<anari::Light>(d, "directional");
    anari::setParameter(d, light, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, light, "irradiance", 2.f);
    anari::commitParameters(d, light);
    anari::setParameterArray1D(d, invalidWorld, "light", &light, 1);
    anari::release(d, light);
    anari::commitParameters(d, invalidWorld);

    // without the ambient light, so that the floor is lit exactly
    Frame f(d,
        invalidWorld,
        "quality",
        [](anari::Device d, anari::Renderer r) {
          anari::setParameter(d, r, "background", BACKGROUND);
        },
        false);
    const auto pixels = f.render();
    // the device's default invalidMaterialColor is (1, 0, 1)
    pass &= report("no material: the invalid material color, lit by the light",
        !pixels.empty()
            && near(pixels[pixelIndex(FLOOR_POS)],
                float4(float3(1.f, 0.f, 1.f) * 2.f / float(M_PI), 1.f),
                1e-5f));
  }
  anari::release(d, invalidWorld);

  return pass;
}

// Renders accumulate: n renders of one sample are the n samples of one
// render (bitwise); a change starts over; renders accumulate without
// 'accumulation' too
bool testAccumulation(anari::Device d, anari::World world)
{
  bool pass = true;
  constexpr int N = 8;

  Frame many(d, world, "quality", withSamples(1), true);
  const auto accumulated = many.render(N);
  Frame once(d, world, "quality", withSamples(N), true);
  const auto direct = once.render();
  pass &= report("N renders of 1 sample == 1 render of N samples",
      sameImage(accumulated, direct, 0.f));

  Frame fresh(d, world, "quality", withSamples(1), true);
  const auto first = fresh.render();
  pass &= report("accumulated renders differ from the first",
      !sameImage(accumulated, first, 1e-3f));

  // a change that leaves the image as it was (helium ignores commits that
  // change no parameter): the camera moving along its view direction
  anari::setParameter(d, many.camera, "position", float3(0.f, 0.f, 4.f));
  anari::commitParameters(d, many.camera);
  pass &= report("a camera change starts over",
      sameImage(many.render(), first, 0.f));

  // the frame's renderer replaced by an equal one
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  withSamples(1)(d, renderer);
  anari::commitParameters(d, renderer);
  many.render(3);
  anari::setParameter(d, many.frame, "renderer", renderer);
  anari::commitParameters(d, many.frame);
  pass &= report("a frame change starts over",
      sameImage(many.render(), first, 0.f));

  // a renderer parameter that doesn't change the image
  many.render(3);
  anari::setParameter(d, renderer, "taskGrainSizeWidth", 8);
  anari::commitParameters(d, renderer);
  pass &= report("a renderer change starts over",
      sameImage(many.render(), first, 0.f));
  anari::release(d, renderer);

  // a world parameter that doesn't change the image
  many.render(3);
  anari::setParameter(d, world, "name", "changed");
  anari::commitParameters(d, world);
  pass &= report("a world change starts over",
      sameImage(many.render(), first, 0.f));

  // the pathtracer refines whatever the frame's 'accumulation' says (a
  // deliberate difference from KHR_FRAME_ACCUMULATION's opt-in)
  Frame still(d, world, "quality", withSamples(1), false);
  const auto stillFirst = still.render();
  pass &= report("without accumulation, renders refine too",
      sameImage(stillFirst, first, 0.f)
          && sameImage(still.render(N - 1), accumulated, 0.f));

  return pass;
}

// Noise falls as renders accumulate: against a 1024-sample reference, the
// error after 64 renders is well below that after 4 (by sqrt(16) = 4 in
// expectation)
bool testConvergence(anari::Device d, anari::World world)
{
  Frame reference(d, world, "quality", withSamples(1024), false);
  const auto ref = reference.render();

  Frame f(d, world, "quality", withSamples(1), true);
  const float early = rmse(f.render(4), ref, 0.5f);
  const float late = rmse(f.render(60), ref, 0.5f);
  std::printf("  rmse after 4 renders %g, after 64 %g\n", early, late);
  return report("accumulation converges", late > 0.f && early > 2.5f * late);
}

// More bounces gather the wall's interreflection: the floor next to it
// brightens
bool testMaxBounces(anari::Device d, anari::World world)
{
  Frame direct(d, world, "quality", withSamples(256, 0), false);
  Frame bounced(d, world, "quality", withSamples(256, 8), false);
  const float a = meanRed(direct.render(), 0.f, 0.5f);
  const float b = meanRed(bounced.render(), 0.f, 0.5f);
  std::printf("  floor near the wall: maxBounces 0 %g, 8 %g\n", a, b);
  return report("maxBounces gathers interreflection", b > a * 1.05f);
}

SetParams withSeed(int seed)
{
  return [=](anari::Device d, anari::Renderer r) {
    withSamples(1)(d, r);
    anari::setParameter(d, r, "randomNumberSeed", seed);
  };
}

// The seed selects the samples: the same seed renders the same image, another
// a different one; -1 (Typhoon's default, the seed of scene frame 0) is seed
// 0; and changing the seed starts the accumulation over
bool testRandomNumberSeed(anari::Device d, anari::World world)
{
  bool pass = true;

  Frame unset(d, world, "quality", withSamples(1), true);
  const auto byDefault = unset.render();
  Frame zero(d, world, "quality", withSeed(0), true);
  pass &= report("randomNumberSeed -1 renders as seed 0",
      sameImage(zero.render(), byDefault, 0.f));

  Frame seven(d, world, "quality", withSeed(7), true);
  const auto first7 = seven.render();
  pass &= report("another seed renders another image",
      !sameImage(first7, byDefault, 1e-3f));
  Frame sevenAgain(d, world, "quality", withSeed(7), true);
  pass &= report("the same seed renders the same image",
      sameImage(sevenAgain.render(), first7, 0.f));

  // the renderer of 'unset' changed to seed 7
  unset.render(3);
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  withSeed(0)(d, renderer);
  anari::commitParameters(d, renderer);
  anari::setParameter(d, unset.frame, "renderer", renderer);
  anari::commitParameters(d, unset.frame);
  unset.render(3);
  anari::setParameter(d, renderer, "randomNumberSeed", 7);
  anari::commitParameters(d, renderer);
  pass &= report("a seed change starts over with the new seed's samples",
      sameImage(unset.render(), first7, 0.f));
  anari::release(d, renderer);

  return pass;
}

// Depth and ids describe the pixel center's visible hit, as the preview
// renderer's
bool testChannels(anari::Device d, anari::World world)
{
  Frame pt(d, world, "quality", withSamples(2), true, true);
  Frame def(d, world, "default", {}, false, true);
  pt.render(2);
  def.render();
  bool pass = true;
  pass &= report("depth matches the preview renderer's",
      pt.channel<float>("channel.depth") == def.channel<float>("channel.depth"));
  pass &= report("primitive ids match the preview renderer's",
      pt.channel<uint32_t>("channel.primitiveId")
          == def.channel<uint32_t>("channel.primitiveId"));
  pass &= report("object ids match the preview renderer's",
      pt.channel<uint32_t>("channel.objectId")
          == def.channel<uint32_t>("channel.objectId"));
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

  bool pass = testIntrospection(library, d);
  pass &= testLambertUnderAmbient(d);
  pass &= testSurfaceColor(d);

  auto corner = makeCornerWorld(d);
  pass &= testAccumulation(d, corner);
  pass &= testConvergence(d, corner);
  pass &= testMaxBounces(d, corner);
  pass &= testChannels(d, corner);
  pass &= testRandomNumberSeed(d, corner);
  anari::release(d, corner);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors", g_errors == 0);
  // only the surface without a material's (testSurfaceColor())
  pass &= report("only the expected warning",
      g_warnings.size() == 1
          && g_warnings[0].find("missing 'material'") != std::string::npos);
  return pass ? 0 : 1;
}
