// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'hdri' light (KHR_LIGHT_HDRI) through the 'quality' renderer:
// introspection (with KHR_LIGHT_PRIMARY_VISIBILITY), the map's orientation
// ('up', 'direction', instance transforms) as camera rays see it, furnace
// tests (a white Lambertian sphere, and a white scattering volume, in a
// uniform map render to the map's radiance: energy conservation of
// importance sampling, folding into the reflection hemisphere, phase
// sampling and MIS), a Lambertian floor under textured maps against the
// irradiance integrated over the map's texels (a smooth sky, a black lower
// hemisphere, and a map whose light comes from one bright texel, at one and
// several light samples), 'visible', 'scale' and 'color', the warnings, and
// the 'preview' renderer showing it where camera rays miss, exactly as the
// pathtracer does, without being lit by it.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr float PI = 3.14159265358979323846f;
constexpr uint2 IMAGE_SIZE = {32, 32};
const float3 ALBEDO = {0.8f, 0.6f, 0.4f};
const float3 LIGHT_COLOR = {1.f, 0.5f, 0.75f};
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

bool report(const std::string &name, bool ok)
{
  return report(name.c_str(), ok);
}

// Maps ///////////////////////////////////////////////////////////////////////

// An equirectangular map as the 'radiance' array holds it: width x height
// texels, row 0 towards -up
struct Map
{
  uint32_t width{1};
  uint32_t height{1};
  std::vector<float3> texels{float3(1.f)};

  float3 &at(uint32_t x, uint32_t y)
  {
    return texels[size_t(y) * width + x];
  }
  const float3 &at(uint32_t x, uint32_t y) const
  {
    return texels[size_t(y) * width + x];
  }
};

Map uniformMap(float3 L, uint32_t width = 8, uint32_t height = 4)
{
  Map m;
  m.width = width;
  m.height = height;
  m.texels.assign(size_t(width) * height, L);
  return m;
}

// The map's frame for 'up' and 'direction' (light/Hdri.h): 'up' kept,
// 'direction' made perpendicular to it, left = up x direction
struct Frame
{
  float3 left;
  float3 up;
  float3 direction;
};

Frame mapFrame(float3 up, float3 direction)
{
  Frame f;
  f.up = normalize(up);
  f.direction = normalize(direction - dot(direction, f.up) * f.up);
  f.left = cross(f.up, f.direction);
  return f;
}

// The world direction of the map's (u, v): u turns from -direction (0)
// through left (0.25), direction (0.5) and -left (0.75); v from -up (0) to
// up (1)
float3 mapDirection(const Frame &f, float u, float v)
{
  const float theta = PI * (1.f - v); // from up
  const float alpha = 2.f * PI * (0.5f - u);
  return std::sin(theta) * (std::sin(alpha) * f.left + std::cos(alpha) * f.direction)
      + std::cos(theta) * f.up;
}

// The irradiance (per channel) of a surface facing the map's 'up' (its
// height even, so that the horizon is a row boundary): each texel of the
// upper hemisphere times the integral of cos(theta) over its solid angle,
// (2 pi / W) / 2 (sin^2 theta0 - sin^2 theta1)
float3 upIrradiance(const Map &m)
{
  float3 E(0.f);
  const float dPhi = 2.f * PI / m.width;
  for (uint32_t y = m.height / 2; y < m.height; y++) {
    const float thetaLow = PI * (1.f - float(y) / m.height); // nearer -up
    const float thetaHigh = PI * (1.f - float(y + 1) / m.height);
    const float sLow = std::sin(thetaLow);
    const float sHigh = std::sin(thetaHigh);
    const float w = 0.5f * dPhi * (sLow * sLow - sHigh * sHigh);
    for (uint32_t x = 0; x < m.width; x++)
      E += m.at(x, y) * w;
  }
  return E;
}

// Scene //////////////////////////////////////////////////////////////////////

using SetParams = std::function<void(anari::Device, anari::Light)>;

anari::Light makeHdri(anari::Device d,
    const Map &m,
    const SetParams &setParams = {},
    float3 color = LIGHT_COLOR)
{
  auto light = anari::newObject<anari::Light>(d, "hdri");
  anari::setParameterArray2D(
      d, light, "radiance", m.texels.data(), m.width, m.height);
  anari::setParameter(d, light, "color", color);
  if (setParams)
    setParams(d, light);
  anari::commitParameters(d, light);
  return light;
}

anari::Material matte(anari::Device d, float3 color)
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

// The floor z = 0 over [-1, 1]^2, facing +z
anari::Surface makeFloor(anari::Device d, float3 color = ALBEDO)
{
  const std::vector<float3> v = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return makeSurface(d, geom, matte(d, color));
}

anari::Surface makeSphere(anari::Device d, float3 color)
{
  const float3 center(0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", 1.f);
  anari::commitParameters(d, geom);
  return makeSurface(d, geom, matte(d, color));
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

// A world of 'surfaces', 'volumes' and 'lights' (all released); the lights
// go in an instance of 'xfm' if 'instanced'
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Light> lights,
    std::vector<anari::Volume> volumes = {},
    bool instanced = false,
    const mat4 &xfm = mat4(linalg::identity))
{
  auto world = anari::newObject<anari::World>(d);
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
  if (!volumes.empty())
    anari::setParameterArray1D(d, world, "volume", volumes.data(), volumes.size());
  if (!lights.empty() && !instanced) {
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  } else if (!lights.empty()) {
    auto group = anari::newObject<anari::Group>(d);
    anari::setParameterArray1D(d, group, "light", lights.data(), lights.size());
    anari::commitParameters(d, group);
    auto inst = anari::newObject<anari::Instance>(d, "transform");
    anari::setAndReleaseParameter(d, inst, "group", group);
    anari::setParameter(d, inst, "transform", xfm);
    anari::commitParameters(d, inst);
    anari::setParameterArray1D(d, world, "instance", &inst, 1);
    anari::release(d, inst);
  }
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
  uint2 size{IMAGE_SIZE};
  float fovy{0.6f};
};

struct Settings
{
  int pixelSamples{1};
  int maxBounces{0};
  int lightSamplesPerHit{1};
  const char *subtype{"quality"};
  // the renderer's 'mode', if set
  const char *mode{nullptr};
};

// The colour channel, and with 'depth' the depth channel into it
std::vector<float4> render(anari::Device d,
    anari::World world,
    const Settings &settings = {},
    const View &view = {},
    std::vector<float> *depth = nullptr)
{
  auto renderer = anari::newObject<anari::Renderer>(d, settings.subtype);
  // one render is the whole first render (no progressive resolution)
  anari::setParameter(d, renderer, "resolutionDivider", 0);
  anari::setParameter(d, renderer, "background", BACKGROUND);
  if (settings.mode)
    anari::setParameter(d, renderer, "mode", settings.mode);
  if (std::strcmp(settings.subtype, "quality") == 0) {
    anari::setParameter(d, renderer, "pixelSamples", settings.pixelSamples);
    anari::setParameter(d, renderer, "maxBounces", settings.maxBounces);
    anari::setParameter(
        d, renderer, "lightSamplesPerHit", settings.lightSamplesPerHit);
    anari::setParameter(d, renderer, "fireflyClampThreshold", 0.f);
  }
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, view.type);
  anari::setParameter(d, camera, "position", view.position);
  anari::setParameter(d, camera, "direction", view.direction);
  anari::setParameter(d, camera, "up", view.up);
  if (std::strcmp(view.type, "orthographic") == 0)
    anari::setParameter(d, camera, "height", 2.f);
  else
    anari::setParameter(d, camera, "fovy", view.fovy);
  anari::setParameter(d, camera, "aspect", float(view.size.x) / view.size.y);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", view.size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  if (depth)
    anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  std::vector<float4> pixels;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == view.size.x && fb.height == view.size.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");
  if (depth) {
    depth->clear();
    auto db = anari::map<float>(d, frame, "channel.depth");
    if (db.data && db.width == view.size.x && db.height == view.size.y)
      depth->assign(db.data, db.data + db.width * db.height);
    anari::unmap(d, frame, "channel.depth");
  }
  anari::release(d, frame);
  return pixels;
}

bool close3(float3 a, float3 b, float tolerance)
{
  return std::abs(a.x - b.x) <= tolerance * std::max(std::abs(b.x), 1e-6f)
      && std::abs(a.y - b.y) <= tolerance * std::max(std::abs(b.y), 1e-6f)
      && std::abs(a.z - b.z) <= tolerance * std::max(std::abs(b.z), 1e-6f);
}

std::string str(float3 v)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", v.x, v.y, v.z);
  return buf;
}

// What a camera ray along world direction 'dir' that misses everything
// sees: an orthographic camera looking along it, over an empty world
float3 seen(anari::Device d, anari::World world, float3 dir)
{
  View view;
  view.direction = dir;
  view.position = float3(0.f);
  view.up = std::abs(dir.z) < 0.9f ? float3(0.f, 0.f, 1.f) : float3(1.f, 0.f, 0.f);
  view.size = {2, 2};
  const auto image = render(d, world, {}, view);
  return image.empty() ? float3(-1.f) : float3(image[0].x, image[0].y, image[0].z);
}

// Tests //////////////////////////////////////////////////////////////////////

bool testIntrospection(anari::Device d)
{
  bool pass = true;
  bool listed = false;
  if (auto *subtypes = anariGetObjectSubtypes(d, ANARI_LIGHT)) {
    for (int i = 0; subtypes[i]; i++)
      listed |= std::strcmp(subtypes[i], "hdri") == 0;
  }
  pass &= report("hdri listed as a light subtype", listed);

  int declared = 0;
  const char *const *extensions = nullptr;
  anariGetProperty(d,
      d,
      "extension",
      ANARI_STRING_LIST,
      &extensions,
      sizeof(extensions),
      ANARI_WAIT);
  for (int i = 0; extensions && extensions[i]; i++) {
    declared += std::strcmp(extensions[i], "ANARI_KHR_LIGHT_HDRI") == 0;
    declared +=
        std::strcmp(extensions[i], "ANARI_KHR_LIGHT_PRIMARY_VISIBILITY") == 0;
  }
  pass &= report(
      "KHR_LIGHT_HDRI and KHR_LIGHT_PRIMARY_VISIBILITY declared", declared == 2);

  for (const char *subtype :
      {"directional", "point", "spot", "quad", "ring", "hdri"}) {
    auto *visible = (const bool *)anariGetParameterInfo(
        d, ANARI_LIGHT, subtype, "visible", ANARI_BOOL, "default", ANARI_BOOL);
    pass &= report(std::string(subtype) + " 'visible' declared, default true",
        visible && *visible);
  }
  auto *up = (const float *)anariGetParameterInfo(
      d, ANARI_LIGHT, "hdri", "up", ANARI_FLOAT32_VEC3, "default", ANARI_FLOAT32_VEC3);
  auto *direction = (const float *)anariGetParameterInfo(d,
      ANARI_LIGHT,
      "hdri",
      "direction",
      ANARI_FLOAT32_VEC3,
      "default",
      ANARI_FLOAT32_VEC3);
  pass &= report("hdri 'up' (0, 0, 1) and 'direction' (1, 0, 0) declared",
      up && direction && up[2] == 1.f && direction[0] == 1.f);
  return pass;
}

// Camera rays that miss see the map: each texel where the mapping puts it,
// for the default frame, another 'up' and 'direction' (not perpendicular:
// 'direction' is made so), and an instance's rotation
bool testOrientation(anari::Device d)
{
  bool pass = true;
  // every texel its own colour
  Map m;
  m.width = 8;
  m.height = 4;
  m.texels.resize(32);
  for (uint32_t y = 0; y < m.height; y++) {
    for (uint32_t x = 0; x < m.width; x++)
      m.at(x, y) = float3(1.f + x, 1.f + 10.f * y, 0.5f);
  }

  struct Case
  {
    const char *name;
    float3 up;
    float3 direction;
    bool instanced;
    mat4 xfm;
  };
  // 90 degrees about +z: x -> y, y -> -x
  const mat4 rotZ = {{0.f, 1.f, 0.f, 0.f},
      {-1.f, 0.f, 0.f, 0.f},
      {0.f, 0.f, 1.f, 0.f},
      {0.f, 0.f, 0.f, 1.f}};
  const Case cases[] = {
      {"default frame", {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, false, mat4(linalg::identity)},
      {"up +y, direction +z (hdAnari's Y-up)",
          {0.f, 1.f, 0.f},
          {0.f, 0.f, 1.f},
          false,
          mat4(linalg::identity)},
      {"direction made perpendicular to up",
          {0.f, 0.f, 1.f},
          {0.f, 1.f, 0.7f},
          false,
          mat4(linalg::identity)},
      {"instance rotation turns the map", {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, true, rotZ},
  };
  for (const auto &c : cases) {
    auto world = makeWorld(d,
        {},
        {makeHdri(d,
            m,
            [&](anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "up", c.up);
              anari::setParameter(d, l, "direction", c.direction);
            })},
        {},
        c.instanced,
        c.xfm);
    float3 up = c.up;
    float3 dir = c.direction;
    if (c.instanced) {
      up = linalg::mul(c.xfm, float4(up, 0.f)).xyz();
      dir = linalg::mul(c.xfm, float4(dir, 0.f)).xyz();
    }
    const Frame f = mapFrame(up, dir);
    bool ok = true;
    for (uint32_t y = 0; y < m.height; y++) {
      for (uint32_t x = 0; x < m.width; x++) {
        const float3 w = mapDirection(
            f, (x + 0.5f) / m.width, (y + 0.5f) / m.height);
        const float3 expected = m.at(x, y) * LIGHT_COLOR;
        const float3 got = seen(d, world, w);
        if (!close3(got, expected, 1e-5f)) {
          std::printf("  %s: texel (%u, %u) seen %s, expected %s\n",
              c.name,
              x,
              y,
              str(got).c_str(),
              str(expected).c_str());
          ok = false;
        }
      }
    }
    pass &= report(std::string("map orientation: ") + c.name, ok);
    anari::release(d, world);
  }

  // the named directions of the default frame: 'direction' (+x) sees the
  // image's center column, 'up' (+z) its last row
  auto world = makeWorld(d, {}, {makeHdri(d, m)});
  const float3 alongDirection = seen(d, world, normalize(float3(1.f, 0.01f, 0.1f)));
  const float3 alongUp = seen(d, world, normalize(float3(0.01f, 0.02f, 1.f)));
  const float3 alongLeft = seen(d, world, normalize(float3(0.01f, 1.f, 0.1f)));
  anari::release(d, world);
  pass &= report("'direction' sees the image's center",
      alongDirection.x == 4.f * LIGHT_COLOR.x || alongDirection.x == 5.f * LIGHT_COLOR.x);
  pass &= report("'up' sees the array's last row",
      std::abs(alongUp.y - 31.f * LIGHT_COLOR.y) < 1e-5f);
  pass &= report("up x direction sees the image's left quarter",
      std::abs(alongLeft.x - 3.f * LIGHT_COLOR.x) < 1e-5f);
  return pass;
}

// A white Lambertian sphere in a uniform map renders to the map's radiance,
// as does every ray that misses it (the furnace test): light sampling of
// the map (folded into the reflection hemisphere), BSDF sampling and MIS
// add to exactly the map's radiance in expectation
bool testFurnace(anari::Device d)
{
  bool pass = true;
  const float3 L = float3(0.7f, 1.2f, 0.4f);
  View view;
  view.type = "perspective";
  view.position = float3(0.f, -4.f, 0.5f);
  view.direction = normalize(float3(0.f, 4.f, -0.5f));
  view.up = float3(0.f, 0.f, 1.f);

  for (int lightSamples : {1, 4}) {
    auto world = makeWorld(
        d, {makeSphere(d, float3(1.f))}, {makeHdri(d, uniformMap(L))});
    Settings s;
    s.pixelSamples = 256 / lightSamples;
    s.lightSamplesPerHit = lightSamples;
    s.maxBounces = 16;
    const auto image = render(d, world, s, view);
    anari::release(d, world);

    const float3 expected = L * LIGHT_COLOR;
    double sum[3] = {};
    float worst = 0.f;
    bool misses = !image.empty();
    for (const auto &p : image) {
      for (int c = 0; c < 3; c++) {
        sum[c] += p[c];
        worst = std::max(worst, std::abs(p[c] - expected[c]) / expected[c]);
      }
      misses &= p.w == 1.f;
    }
    const float3 mean = image.empty()
        ? float3(0.f)
        : float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(image.size());
    const std::string suffix =
        " (lightSamplesPerHit " + std::to_string(lightSamples) + ")";
    if (!close3(mean, expected, 0.003f) || worst > 0.1f) {
      std::printf("  mean %s, expected %s, worst pixel error %g\n",
          str(mean).c_str(),
          str(expected).c_str(),
          worst);
    }
    pass &= report("furnace: the image's mean is the map's radiance" + suffix,
        close3(mean, expected, 0.003f));
    pass &= report("furnace: every pixel is near the map's radiance" + suffix,
        worst <= 0.1f);
    pass &= report("furnace: the map is opaque" + suffix, misses);
  }
  return pass;
}

// A box of white scattering medium in a uniform map renders to the map's
// radiance: the map's light sampling over the whole sphere at volume
// collisions, phase sampling and MIS
bool testVolumeFurnace(anari::Device d)
{
  const float3 L = float3(0.5f, 1.f, 1.5f);
  auto world = makeWorld(
      d, {}, {makeHdri(d, uniformMap(L))}, {makeWhiteVolume(d, 0.5f)});
  Settings s;
  s.pixelSamples = 64;
  s.maxBounces = 256;
  View view;
  view.position = float3(0.3f, -3.f, 0.2f);
  view.direction = float3(0.f, 1.f, 0.f);
  view.up = float3(0.f, 0.f, 1.f);
  const auto image = render(d, world, s, view);
  anari::release(d, world);

  const float3 expected = L * LIGHT_COLOR;
  double sum[3] = {};
  for (const auto &p : image) {
    for (int c = 0; c < 3; c++)
      sum[c] += p[c];
  }
  const float3 mean = image.empty()
      ? float3(0.f)
      : float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(image.size());
  if (!close3(mean, expected, 0.01f))
    std::printf("  mean %s, expected %s\n", str(mean).c_str(), str(expected).c_str());
  return report("volume furnace: a white medium renders to the map's radiance",
      close3(mean, expected, 0.01f));
}

// A Lambertian floor facing the map's 'up' shows albedo / pi times the
// irradiance of the map's upper hemisphere in every pixel
bool floorMatches(anari::Device d,
    const char *name,
    const Map &m,
    float scale,
    int pixelSamples,
    int lightSamples,
    float pixelTolerance,
    float meanTolerance,
    bool visible = true)
{
  auto world = makeWorld(d,
      {makeFloor(d)},
      {makeHdri(d, m, [&](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "scale", scale);
        if (!visible)
          anari::setParameter(d, l, "visible", false);
      })});
  Settings s;
  s.pixelSamples = pixelSamples;
  s.lightSamplesPerHit = lightSamples;
  const auto image = render(d, world, s);
  anari::release(d, world);

  const float3 expected = ALBEDO / PI * LIGHT_COLOR * scale * upIrradiance(m);
  double sum[3] = {};
  float worst = 0.f;
  for (const auto &p : image) {
    for (int c = 0; c < 3; c++) {
      sum[c] += p[c];
      worst = std::max(worst, std::abs(p[c] - expected[c]) / expected[c]);
    }
  }
  const float3 mean = image.empty()
      ? float3(0.f)
      : float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(image.size());
  const bool ok = !image.empty() && close3(mean, expected, meanTolerance)
      && worst <= pixelTolerance;
  if (!ok) {
    std::printf("  %s: mean %s, expected %s, worst pixel error %g\n",
        name,
        str(mean).c_str(),
        str(expected).c_str(),
        worst);
  }
  return report(name, ok);
}

bool testIrradiance(anari::Device d)
{
  bool pass = true;

  // a smooth sky: brighter towards 'up', tinted by azimuth
  Map sky;
  sky.width = 32;
  sky.height = 16;
  sky.texels.resize(size_t(sky.width) * sky.height);
  for (uint32_t y = 0; y < sky.height; y++) {
    for (uint32_t x = 0; x < sky.width; x++) {
      const float v = (y + 0.5f) / sky.height;
      const float u = (x + 0.5f) / sky.width;
      sky.at(x, y) = float3(0.2f + 2.f * v * v,
          0.5f + 0.4f * std::sin(2.f * PI * u),
          0.3f + v);
    }
  }
  pass &= floorMatches(d, "floor under a smooth sky", sky, 1.f, 64, 1, 0.08f, 0.005f);
  // (several light samples are less evenly spread over a pixel's samples
  // than one, so the pixels are noisier)
  pass &= floorMatches(d,
      "floor under a smooth sky, 4 light samples, 'scale' 2.5",
      sky,
      2.5f,
      64,
      4,
      0.15f,
      0.005f);

  // the lower hemisphere far brighter than the upper: folding samples into
  // the reflection hemisphere keeps them all
  Map ground = sky;
  for (uint32_t y = 0; y < ground.height / 2; y++) {
    for (uint32_t x = 0; x < ground.width; x++)
      ground.at(x, y) = float3(50.f);
  }
  pass &= floorMatches(
      d, "a bright lower hemisphere doesn't light the floor", ground, 1.f, 64, 1, 0.08f, 0.005f);

  // all the light from one bright texel ('the sun') over a dim sky, and
  // black rows (sampled by solid angle alone)
  Map sun = uniformMap(float3(0.05f), 64, 32);
  for (uint32_t x = 0; x < sun.width; x++)
    sun.at(x, 31) = float3(0.f);
  sun.at(20, 25) = float3(2000.f, 1500.f, 1000.f);
  pass &= floorMatches(d, "floor under a sun texel", sun, 1.f, 64, 1, 0.08f, 0.005f);
  pass &= floorMatches(d,
      "floor under a sun texel, not visible to the camera",
      sun,
      1.f,
      64,
      1,
      0.08f,
      0.005f,
      false);
  return pass;
}

// 'visible' false: camera rays see the background (and its alpha), and the
// map still lights the scene; a visible hdri of 'scale' 0 is seen black
bool testVisibility(anari::Device d)
{
  bool pass = true;
  const Map m = uniformMap(float3(2.f));
  auto world = makeWorld(d, {}, {makeHdri(d, m, [](anari::Device d, anari::Light l) {
    anari::setParameter(d, l, "visible", false);
  })});
  View view;
  view.size = {2, 2};
  auto image = render(d, world, {}, view);
  anari::release(d, world);
  pass &= report("an invisible hdri shows the background",
      !image.empty() && image[0].x == BACKGROUND.x && image[0].w == BACKGROUND.w);

  world = makeWorld(d, {}, {makeHdri(d, m)});
  image = render(d, world, {}, view);
  anari::release(d, world);
  pass &= report("a visible hdri hides the background, opaque",
      !image.empty() && image[0].x == 2.f * LIGHT_COLOR.x && image[0].w == 1.f);

  world = makeWorld(d, {}, {makeHdri(d, m, [](anari::Device d, anari::Light l) {
    anari::setParameter(d, l, "scale", 0.f);
  })});
  image = render(d, world, {}, view);
  anari::release(d, world);
  pass &= report("a visible hdri of 'scale' 0 is seen black",
      !image.empty() && image[0].x == 0.f && image[0].w == 1.f);

  world = makeWorld(d, {}, {makeHdri(d, m, [](anari::Device d, anari::Light l) {
    anari::setParameter(d, l, "scale", 0.f);
    anari::setParameter(d, l, "visible", false);
  })});
  image = render(d, world, {}, view);
  anari::release(d, world);
  pass &= report("an invisible hdri of 'scale' 0 isn't there",
      !image.empty() && image[0].x == BACKGROUND.x);

  // two visible maps add
  world = makeWorld(d, {}, {makeHdri(d, m), makeHdri(d, uniformMap(float3(1.f)))});
  image = render(d, world, {}, view);
  anari::release(d, world);
  pass &= report("visible hdris add",
      !image.empty() && std::abs(image[0].x - 3.f * LIGHT_COLOR.x) < 1e-5f);
  return pass;
}

// A map whose every texel has its own colour, most above 1 (a miss shows
// the map unclamped in both renderers)
Map distinctMap(uint32_t width, uint32_t height)
{
  Map m;
  m.width = width;
  m.height = height;
  m.texels.resize(size_t(width) * height);
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++)
      m.at(x, y) = float3(0.5f + x, 0.25f + 3.f * y, 0.1f * (x + y));
  }
  return m;
}

bool equal4(float4 a, float4 b)
{
  return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

std::string str(float4 v)
{
  char buf[128];
  std::snprintf(buf, sizeof(buf), "(%g, %g, %g, %g)", v.x, v.y, v.z, v.w);
  return buf;
}

bool isMiss(float depth)
{
  return depth >= std::numeric_limits<float>::max();
}

// A wide perspective view, so that misses see several texels
View wideView()
{
  View view;
  view.type = "perspective";
  view.position = float3(0.f, -1.6f, 0.6f);
  view.direction = normalize(float3(0.15f, 1.f, -0.45f));
  view.up = float3(0.f, 0.f, 1.f);
  view.fovy = 2.f;
  return view;
}

// The 'preview' renderer (in 'mode', if set) and the 'quality' show
// 'world''s misses alike: every pixel whose ray and its 8 neighbours' miss
// in the 'preview' renderer and show one colour there (so that the
// pathtracer's jittered samples in it see the same texel) is bitwise the
// pathtracer's, colour and alpha. 'compared' counts those pixels.
bool missesMatch(anari::Device d,
    anari::World world,
    const View &view,
    const char *mode,
    int &compared)
{
  Settings def;
  def.subtype = "default";
  def.mode = mode;
  std::vector<float> depth;
  const auto a = render(d, world, def, view, &depth);
  Settings pt;
  pt.pixelSamples = 4;
  const auto b = render(d, world, pt, view);
  compared = 0;
  if (a.empty() || b.empty() || depth.size() != a.size())
    return false;

  const int w = int(view.size.x);
  const int h = int(view.size.y);
  bool ok = true;
  for (int y = 1; y + 1 < h; y++) {
    for (int x = 1; x + 1 < w; x++) {
      const int i = y * w + x;
      bool uniform = true;
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          const int n = (y + dy) * w + x + dx;
          uniform &= isMiss(depth[n]) && equal4(a[n], a[i]);
        }
      }
      if (!uniform)
        continue;
      compared++;
      if (!equal4(a[i], b[i])) {
        if (ok) {
          std::printf("  pixel (%d, %d): 'preview' %s, 'quality' %s\n",
              x,
              y,
              str(a[i]).c_str(),
              str(b[i]).c_str());
        }
        ok = false;
      }
    }
  }
  return ok;
}

// The 'preview' renderer isn't lit by hdri lights but shows the 'visible'
// ones where its camera rays miss, as the pathtracer does: miss pixels
// match the pathtracer's exactly (also through an instance rotation, in the
// debug and ambient occlusion modes and around a floor), 'visible' false
// shows the background, two visible hdris sum, and surfaces render exactly
// as without the light
bool testDefaultRendererShowsHdri(anari::Device d)
{
  bool pass = true;
  const Map m = distinctMap(16, 8);
  const View view = wideView();
  const int pixels = int(view.size.x * view.size.y);
  // 90 degrees about +z: x -> y, y -> -x
  const mat4 rotZ = {{0.f, 1.f, 0.f, 0.f},
      {-1.f, 0.f, 0.f, 0.f},
      {0.f, 0.f, 1.f, 0.f},
      {0.f, 0.f, 0.f, 1.f}};

  auto matches = [&](const std::string &name,
                     anari::World world,
                     const char *mode,
                     int minCompared) {
    int compared = 0;
    const bool ok = missesMatch(d, world, view, mode, compared);
    if (compared < minCompared)
      std::printf("  %s: only %d pixels compared\n", name.c_str(), compared);
    return report("'preview' renderer misses show the hdri as the pathtracer's: "
            + name,
        ok && compared >= minCompared);
  };

  {
    auto world = makeWorld(d, {}, {makeHdri(d, m)});
    pass &= matches("no surfaces", world, nullptr, pixels / 4);
    anari::release(d, world);
  }
  {
    auto world = makeWorld(d, {}, {makeHdri(d, m)}, {}, true, rotZ);
    pass &= matches("instance rotation", world, nullptr, pixels / 4);
    // the rotation turns what the misses see
    Settings def;
    def.subtype = "default";
    auto unrotated = makeWorld(d, {}, {makeHdri(d, m)});
    const auto a = render(d, world, def, view);
    const auto b = render(d, unrotated, def, view);
    anari::release(d, unrotated);
    int differ = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
      differ += !equal4(a[i], b[i]);
    pass &= report("the instance rotation turns the 'preview' renderer's hdri",
        differ > pixels / 2);
    anari::release(d, world);
  }
  {
    auto world = makeWorld(d, {makeFloor(d)}, {makeHdri(d, m)});
    pass &= matches("around a floor", world, nullptr, pixels / 8);
    pass &= matches("'Ng' mode", world, "Ng", pixels / 8);
    pass &= matches("'primitiveId' mode", world, "primitiveId", pixels / 8);
    pass &= matches("'hitVolume' mode", world, "hitVolume", pixels / 8);
    pass &= matches("'ambientOcclusion' mode", world, "ambientOcclusion", pixels / 8);
    pass &= matches("'ambientOcclusion.unblurred' mode",
        world,
        "ambientOcclusion.unblurred",
        pixels / 8);

    // surfaces render as without the light, in the default mode and the
    // 'ambientOcclusion' mode
    auto unlit = makeWorld(d, {makeFloor(d)}, {});
    for (const char *mode : {"default", "ambientOcclusion"}) {
      Settings def;
      def.subtype = "default";
      def.mode = mode;
      std::vector<float> depth;
      const auto a = render(d, world, def, view, &depth);
      const auto b = render(d, unlit, def, view);
      bool ok = !a.empty() && a.size() == b.size() && depth.size() == a.size();
      int hits = 0;
      for (size_t i = 0; ok && i < a.size(); i++) {
        const bool same =
            isMiss(depth[i]) ? equal4(b[i], BACKGROUND) : equal4(a[i], b[i]);
        hits += !isMiss(depth[i]);
        if (!same) {
          std::printf("  pixel %zu (depth %g): with the hdri %s, without %s\n",
              i,
              depth[i],
              str(a[i]).c_str(),
              str(b[i]).c_str());
        }
        ok &= same;
      }
      if (!(ok && hits > pixels / 16))
        std::printf("  %d hits of %d pixels\n", hits, int(a.size()));
      pass &= report(std::string("the hdri doesn't light surfaces in the "
                                 "'preview' renderer ('")
              + mode + "' mode)",
          ok && hits > pixels / 16);
    }
    anari::release(d, unlit);
    anari::release(d, world);
  }
  {
    // 'visible' false: both renderers show the background
    auto world = makeWorld(d, {}, {makeHdri(d, m, [](anari::Device d, anari::Light l) {
      anari::setParameter(d, l, "visible", false);
    })});
    Settings def;
    def.subtype = "default";
    const auto a = render(d, world, def, view);
    Settings pt;
    const auto b = render(d, world, pt, view);
    anari::release(d, world);
    bool ok = !a.empty() && a.size() == b.size();
    for (size_t i = 0; ok && i < a.size(); i++)
      ok &= equal4(a[i], BACKGROUND) && equal4(b[i], BACKGROUND);
    pass &= report("an hdri with 'visible' false shows the background in both "
                   "renderers",
        ok);
  }
  {
    // two visible hdris sum (and an invisible one adds nothing)
    Map other = uniformMap(float3(0.25f, 0.5f, 2.f), 4, 2);
    other.at(1, 1) = float3(3.f, 0.f, 1.f);
    auto setOther = [](anari::Device d, anari::Light l) {
      anari::setParameter(d, l, "up", float3(0.f, 1.f, 0.f));
      anari::setParameter(d, l, "direction", float3(0.f, 0.f, 1.f));
    };
    auto both = makeWorld(d,
        {},
        {makeHdri(d, m),
            makeHdri(d, other, setOther, float3(1.f)),
            makeHdri(d,
                uniformMap(float3(7.f)),
                [](anari::Device d, anari::Light l) {
                  anari::setParameter(d, l, "visible", false);
                })});
    pass &= matches("two visible hdris", both, nullptr, pixels / 4);

    auto first = makeWorld(d, {}, {makeHdri(d, m)});
    auto second = makeWorld(d, {}, {makeHdri(d, other, setOther, float3(1.f))});
    Settings def;
    def.subtype = "default";
    const auto a = render(d, both, def, view);
    const auto b = render(d, first, def, view);
    const auto c = render(d, second, def, view);
    anari::release(d, both);
    anari::release(d, first);
    anari::release(d, second);
    bool ok = !a.empty() && a.size() == b.size() && a.size() == c.size();
    for (size_t i = 0; ok && i < a.size(); i++) {
      const float4 sum(b[i].x + c[i].x, b[i].y + c[i].y, b[i].z + c[i].z, 1.f);
      ok &= equal4(a[i], sum);
    }
    pass &= report("the 'preview' renderer shows the sum of the visible hdris",
        ok);
  }
  return pass;
}

bool testWarnings(anari::Device d)
{
  bool pass = true;
  auto warns = [&](const char *name,
                   const std::function<anari::Light()> &makeLight,
                   const char *expected,
                   bool lights) {
    g_warnings.clear();
    auto world = makeWorld(d, {}, {makeLight()});
    View view;
    view.size = {2, 2};
    const auto image = render(d, world, {}, view);
    anari::release(d, world);
    bool ok = g_warnings.size() == 1
        && g_warnings[0].find(expected) != std::string::npos;
    if (!ok) {
      for (auto &w : g_warnings)
        std::printf("  warning: %s\n", w.c_str());
    }
    ok &= !image.empty() && (image[0].x != BACKGROUND.x) == lights;
    g_warnings.clear();
    return report(name, ok);
  };

  pass &= warns(
      "missing 'radiance' warns, the light is invalid",
      [&] {
        // (helium finalizes only objects with a parameter set)
        auto l = anari::newObject<anari::Light>(d, "hdri");
        anari::setParameter(d, l, "scale", 2.f);
        anari::commitParameters(d, l);
        return l;
      },
      "'radiance' is missing",
      false);
  pass &= warns(
      "'radiance' of FLOAT32_VEC4 warns, the light is invalid",
      [&] {
        auto l = anari::newObject<anari::Light>(d, "hdri");
        const float4 t[2] = {float4(1.f), float4(1.f)};
        anari::setParameterArray2D(d, l, "radiance", t, 2, 1);
        anari::commitParameters(d, l);
        return l;
      },
      "FLOAT32_VEC3",
      false);
  pass &= warns(
      "'up' parallel to 'direction' warns, the light is invalid",
      [&] {
        return makeHdri(d, uniformMap(float3(1.f)), [](anari::Device d, anari::Light l) {
          anari::setParameter(d, l, "up", float3(2.f, 0.f, 0.f));
        });
      },
      "not parallel",
      false);
  pass &= warns(
      "an unknown 'layout' warns, equirectangular is used",
      [&] {
        return makeHdri(d, uniformMap(float3(1.f)), [](anari::Device d, anari::Light l) {
          anari::setParameter(d, l, "layout", "cube");
        });
      },
      "'layout'",
      true);
  pass &= warns(
      "negative 'radiance' values warn and count as 0",
      [&] {
        Map m = uniformMap(float3(1.f));
        m.at(0, 0) = float3(-1.f);
        return makeHdri(d, m);
      },
      "negative or non-finite",
      true);
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

  bool pass = testIntrospection(d);
  pass &= testOrientation(d);
  pass &= testFurnace(d);
  pass &= testVolumeFurnace(d);
  pass &= testIrradiance(d);
  pass &= testVisibility(d);
  pass &= testDefaultRendererShowsHdri(d);
  pass &= report("no warnings", g_warnings.empty());
  pass &= testWarnings(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors", g_errors == 0);
  return pass ? 0 : 1;
}
