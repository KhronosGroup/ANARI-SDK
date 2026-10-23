// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The lights (KHR_LIGHT_DIRECTIONAL, _POINT, _SPOT, _QUAD, _RING) through the
// 'quality' renderer: a luminance test per light and brightness
// parameter (a lone Lambertian floor under the light, whose pixels must show
// albedo / pi times the irradiance the ANARI units define,
// light/Light.h, computed in closed form where there is one and by
// quadrature over the light where there isn't), spot and ring cones, quad
// sides, 'intensityDistribution', instance transforms (which move lights and
// map a quad's edges, but don't scale radii), shadows, camera visibility
// (KHR_LIGHT_PRIMARY_VISIBILITY's 'visible'), introspection, the warnings,
// and the 'preview' renderer ignoring lights.

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
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr float PI = 3.14159265358979323846f;
constexpr uint2 IMAGE_SIZE = {32, 32};
// the floor's albedo, and the color of every light
const float3 ALBEDO = {0.8f, 0.6f, 0.4f};
const float3 LIGHT_COLOR = {1.f, 0.5f, 0.75f};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};

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

using SetParams = std::function<void(anari::Device, anari::Light)>;

anari::Light makeLight(
    anari::Device d, const char *subtype, const SetParams &setParams)
{
  auto light = anari::newObject<anari::Light>(d, subtype);
  anari::setParameter(d, light, "color", LIGHT_COLOR);
  setParams(d, light);
  anari::commitParameters(d, light);
  return light;
}

// A quad with corners 'v' (counter-clockwise seen from its front) and a
// matte material of 'color'
anari::Surface makeQuad(
    anari::Device d, const std::vector<float3> &v, float3 color)
{
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

// The floor z = 0 over the camera's view, [-1, 1]^2
anari::Surface makeFloor(anari::Device d)
{
  return makeQuad(d,
      {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}},
      ALBEDO);
}

// A world of 'surfaces' and 'lights' (taking over the references); lights
// go in an instance of 'xfm' if 'instanced'
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Light> lights,
    bool instanced = false,
    const mat4 &xfm = mat4(linalg::identity))
{
  auto world = anari::newObject<anari::World>(d);
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
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
  for (auto l : lights)
    anari::release(d, l);
  return world;
}

// Renders 'world' once through an orthographic camera at z = 3 looking down
// -z at [-1, 1]^2 with a renderer of 'subtype': for the pathtracer,
// 'pixelSamples' paths per pixel, no bounces after the first vertex (rays
// leaving the floor escape) and no ambient light, so every pixel is the
// floor's direct light
std::vector<float4> render(anari::Device d,
    anari::World world,
    int pixelSamples = 1,
    const char *subtype = "quality")
{
  auto renderer = anari::newObject<anari::Renderer>(d, subtype);
  anari::setParameter(d, renderer, "background", BACKGROUND);
  if (std::strcmp(subtype, "quality") == 0) {
    anari::setParameter(d, renderer, "pixelSamples", pixelSamples);
    anari::setParameter(d, renderer, "maxBounces", 0);
  }
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
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

// The floor point at the center of pixel (x, y), offset by 'sub' in pixels
float3 floorPoint(uint32_t x, uint32_t y, float2 sub = float2(0.5f))
{
  return float3((x + sub.x) / IMAGE_SIZE.x * 2.f - 1.f,
      (y + sub.y) / IMAGE_SIZE.y * 2.f - 1.f,
      0.f);
}

// Irradiance on the floor (normal +z) at a point
using Irradiance = std::function<float(float3)>;

// The irradiance averaged over pixel (x, y), as the jittered paths through
// it see it (where it varies over the pixel, a render needs enough samples
// per pixel to average it), from n x n points of the pixel
float pixelIrradiance(const Irradiance &E, uint32_t x, uint32_t y, int n = 8)
{
  double sum = 0.0;
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++)
      sum += E(floorPoint(x, y, float2((i + 0.5f) / n, (j + 0.5f) / n)));
  }
  return float(sum / (n * n));
}

// How matchesIrradiance() compares: each pixel's error relative to its
// expected value, but at least 'floor' times the largest expected value
// (where the light falls off to nothing, noise and the edges of cones
// within a pixel make relative errors meaningless); the irradiance averaged
// over n x n points of each pixel ('subsamples')
struct Check
{
  float pixelTolerance;
  float meanTolerance;
  float floor{0.f};
  int subsamples{8};
};

// Whether every pixel of 'image' shows albedo / pi times the light's color
// times irradiance 'E': the largest relative error over the pixels below
// 'pixelTolerance', and that of the image's mean below 'meanTolerance'
bool matchesIrradiance(const char *name,
    const std::vector<float4> &image,
    const Irradiance &E,
    const Check &check)
{
  if (image.empty())
    return report(name, false);
  std::vector<float3> expected(image.size());
  float largest = 0.f;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float3 e = ALBEDO / PI * LIGHT_COLOR
          * pixelIrradiance(E, x, y, check.subsamples);
      expected[size_t(y) * IMAGE_SIZE.x + x] = e;
      largest = std::max(largest, linalg::maxelem(e));
    }
  }
  double sum = 0.0;
  double expectedSum = 0.0;
  float maxError = 0.f;
  size_t worst = 0;
  for (size_t i = 0; i < image.size(); i++) {
    for (int c = 0; c < 3; c++) {
      const float error = std::abs(image[i][c] - expected[i][c])
          / std::max({expected[i][c], check.floor * largest, 1e-6f});
      if (error > maxError) {
        maxError = error;
        worst = i;
      }
      sum += image[i][c];
      expectedSum += expected[i][c];
    }
  }
  const float meanError = float(std::abs(sum - expectedSum) / expectedSum);
  const bool ok =
      maxError <= check.pixelTolerance && meanError <= check.meanTolerance;
  if (!ok) {
    std::printf(
        "  %s: largest pixel error %g (allowed %g), mean error %g "
        "(allowed %g); worst pixel (%zu, %zu): %g %g %g, expected %g %g "
        "%g\n",
        name,
        maxError,
        check.pixelTolerance,
        meanError,
        check.meanTolerance,
        worst % IMAGE_SIZE.x,
        worst / IMAGE_SIZE.x,
        image[worst].x,
        image[worst].y,
        image[worst].z,
        expected[worst].x,
        expected[worst].y,
        expected[worst].z);
  }
  return report(name, ok);
}

bool matchesIrradiance(const char *name,
    const std::vector<float4> &image,
    const Irradiance &E,
    float pixelTolerance,
    float meanTolerance)
{
  return matchesIrradiance(
      name, image, E, Check{pixelTolerance, meanTolerance});
}

bool luminance(anari::Device d,
    const char *name,
    std::vector<anari::Light> lights,
    const Irradiance &E,
    int pixelSamples,
    const Check &check,
    bool instanced = false,
    const mat4 &xfm = mat4(linalg::identity))
{
  auto world = makeWorld(d, {makeFloor(d)}, lights, instanced, xfm);
  const bool ok =
      matchesIrradiance(name, render(d, world, pixelSamples), E, check);
  anari::release(d, world);
  return ok;
}

bool luminance(anari::Device d,
    const char *name,
    std::vector<anari::Light> lights,
    const Irradiance &E,
    int pixelSamples,
    float pixelTolerance,
    float meanTolerance,
    bool instanced = false,
    const mat4 &xfm = mat4(linalg::identity))
{
  return luminance(d,
      name,
      lights,
      E,
      pixelSamples,
      Check{pixelTolerance, meanTolerance},
      instanced,
      xfm);
}

// Irradiance at floor point 'p' from a light at 'pos' of intensity 'I'
// (W/sr), a point or a sphere wholly above the floor
float pointIrradiance(float3 p, float3 pos, float I)
{
  const float3 v = pos - p;
  const float d = linalg::length(v);
  return I * (v.z / d) / (d * d);
}

// The cone weight of a spot or ring light 'theta' radians off its axis: 1
// inside the core cone openingAngle / 2 - falloffAngle, 0 outside
// openingAngle / 2, a smoothstep in the angle between
float coneWeight(float theta, float openingAngle, float falloffAngle)
{
  const float outer = 0.5f * openingAngle;
  const float inner = outer - std::min(falloffAngle, outer);
  if (outer == inner)
    return theta <= inner ? 1.f : 0.f;
  const float t = std::clamp((theta - inner) / (outer - inner), 0.f, 1.f);
  return 1.f - t * t * (3.f - 2.f * t);
}

// 2 pi times the integral of f(gamma) sin(gamma) over [0, gammaMax]
// (Simpson's rule): a profile symmetric about the axis integrated over the
// solid angle up to gammaMax off it
template <typename F>
double solidAngleIntegral(F &&f, double gammaMax)
{
  constexpr int N = 20000;
  const double h = gammaMax / N;
  double sum = 0.0;
  for (int i = 0; i <= N; i++) {
    const double g = i * h;
    const double w = i == 0 || i == N ? 1.0 : (i % 2 ? 4.0 : 2.0);
    sum += w * f(g) * std::sin(g);
  }
  return 2.0 * PI * sum * h / 3.0;
}

// The angle between unit vectors
float angleBetween(float3 a, float3 b)
{
  return std::acos(std::clamp(linalg::dot(a, b), -1.f, 1.f));
}

// Irradiance on the floor at 'p' from the polygon 'v' of uniform radiance
// 'L' wholly above the floor and facing p (Lambert's polygon formula)
float polygonIrradiance(float3 p, const std::vector<float3> &v, float L)
{
  double sum = 0.0;
  for (size_t i = 0; i < v.size(); i++) {
    const float3 r0 = linalg::normalize(v[i] - p);
    const float3 r1 = linalg::normalize(v[(i + 1) % v.size()] - p);
    sum += angleBetween(r0, r1) * linalg::normalize(linalg::cross(r0, r1)).z;
  }
  return float(0.5 * L * std::abs(sum));
}

// Irradiance on the floor at height h below the center of a disk of radius
// R and uniform radiance L facing down, offset r sideways
float diskIrradiance(float h, float r, float R, float L)
{
  if (R <= 0.f)
    return 0.f;
  const double a = double(h) * h + double(r) * r;
  const double R2 = double(R) * R;
  return float(L * PI / 2.0
      * (1.0 - (a - R2) / std::sqrt((a + R2) * (a + R2) - 4.0 * r * r * R2)));
}

// A table of intensities over gamma (values over [0, pi]) and the C
// half-planes (numC rows over [0, 2 pi), row 0 at C = 0), interpolated
// linearly (and periodically in C), as the spec's C-gamma mapping
struct Distribution
{
  std::vector<float> values;
  int numGamma;
  int numC{1};

  float operator()(float gamma, float C) const
  {
    const float u = std::clamp(gamma / PI, 0.f, 1.f) * (numGamma - 1);
    const int i = std::min(int(u), numGamma - 2);
    const float fu = u - i;
    auto row = [&](int c) {
      const float *r = values.data() + size_t(c) * numGamma;
      return (1.f - fu) * r[i] + fu * r[i + 1];
    };
    if (numC == 1)
      return row(0);
    C = std::fmod(C, 2.f * PI);
    if (C < 0.f)
      C += 2.f * PI;
    const float v = C / (2.f * PI) * numC;
    const int j = std::min(int(v), numC - 1);
    const float fv = v - j;
    return (1.f - fv) * row(j) + fv * row((j + 1) % numC);
  }

  // averaged over C
  float mean(float gamma) const
  {
    if (numC == 1)
      return (*this)(gamma, 0.f);
    double sum = 0.0;
    for (int c = 0; c < numC; c++)
      sum += (*this)(gamma, 2.f * PI * c / numC);
    return float(sum / numC);
  }
};

// The intensity of a light of intensity I along 'axis' towards unit 'e',
// with the C half-planes measured from c0 right-handed about the axis, as
// D(gamma, C) gives it
float distributedIntensity(
    const Distribution &D, float3 axis, float3 c0, float3 e, float I)
{
  const float3 c90 = linalg::cross(axis, c0);
  const float C = std::atan2(linalg::dot(e, c90), linalg::dot(e, c0));
  return I * D(angleBetween(e, axis), C);
}

// Irradiance on the floor at 'p' from a surface given by n x n patches:
// 'patch(i, j)' returns the patch's center, and 'intensity(S, e)' what the
// patch at S of area 'dA' radiates per unit area towards unit e (radiance
// times the cosine off its normal); the patches face p
template <typename Patch, typename Intensity>
float surfaceIrradiance(
    float3 p, int n, float dA, Patch &&patch, Intensity &&intensity)
{
  double sum = 0.0;
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) {
      const float3 S = patch(i, j);
      const float3 v = p - S;
      const float d2 = linalg::dot(v, v);
      const float3 e = v / std::sqrt(d2);
      const float cosFloor = -e.z;
      if (cosFloor > 0.f)
        sum += intensity(S, e) * cosFloor / d2 * dA;
    }
  }
  return float(sum);
}

// Tests /////////////////////////////////////////////////////////////////////

bool testIntrospection(anari::Device d)
{
  bool pass = true;
  const char *const lights[] = {"directional", "point", "spot", "quad", "ring"};
  const char *const extensions[] = {"ANARI_KHR_LIGHT_DIRECTIONAL",
      "ANARI_KHR_LIGHT_POINT",
      "ANARI_KHR_LIGHT_SPOT",
      "ANARI_KHR_LIGHT_QUAD",
      "ANARI_KHR_LIGHT_RING"};

  int listed = 0;
  if (auto *subtypes = anariGetObjectSubtypes(d, ANARI_LIGHT)) {
    for (int i = 0; subtypes[i]; i++) {
      for (const char *light : lights)
        listed += std::strcmp(subtypes[i], light) == 0;
    }
  }
  pass &=
      report("directional, point, spot, quad and ring listed as light subtypes",
          listed == 5);

  int declared = 0;
  const char *const *deviceExtensions = nullptr;
  anariGetProperty(d,
      d,
      "extension",
      ANARI_STRING_LIST,
      &deviceExtensions,
      sizeof(deviceExtensions),
      ANARI_WAIT);
  for (int i = 0; deviceExtensions && deviceExtensions[i]; i++) {
    for (const char *extension : extensions)
      declared += std::strcmp(deviceExtensions[i], extension) == 0;
  }
  pass &= report(
      "KHR_LIGHT_DIRECTIONAL, _POINT, _SPOT, _QUAD and _RING "
      "declared",
      declared == 5);

  for (const char *subtype : lights) {
    auto *visible = (const bool *)anariGetParameterInfo(
        d, ANARI_LIGHT, subtype, "visible", ANARI_BOOL, "default", ANARI_BOOL);
    const std::string name =
        std::string(subtype) + " 'visible' declared, default true";
    pass &= report(name.c_str(), visible && *visible);
  }
  return pass;
}

bool testDirectional(anari::Device d)
{
  bool pass = true;
  // tilted towards +x, at cos 0.8 to the floor's normal
  const float3 tilted = linalg::normalize(float3(-0.6f, 0.f, -0.8f));

  // a delta direction: exact
  pass &= luminance(
      d,
      "directional irradiance, delta",
      {makeLight(d,
          "directional",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "direction", tilted);
            anari::setParameter(d, l, "irradiance", 2.f);
          })},
      [](float3) { return 2.f * 0.8f; },
      1,
      1e-5f,
      1e-5f);

  // the default brightness: irradiance 1, straight down
  pass &= luminance(
      d,
      "directional defaults (irradiance 1, direction -z)",
      {makeLight(d, "directional", [](anari::Device, anari::Light) {})},
      [](float3) { return 1.f; },
      1,
      1e-5f,
      1e-5f);

  // a cone: a surface facing it receives 'irradiance' whatever its size
  // (and E cos(tilt) tilted, as the cone is symmetric about its axis)
  pass &= luminance(
      d,
      "directional irradiance, cone of 0.5 rad",
      {makeLight(d,
          "directional",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "direction", tilted);
            anari::setParameter(d, l, "irradiance", 2.f);
            anari::setParameter(d, l, "angularDiameter", 0.5f);
            // irradiance wins over radiance
            anari::setParameter(d, l, "radiance", 100.f);
          })},
      [](float3) { return 2.f * 0.8f; },
      64,
      0.02f,
      0.002f);

  // radiance: L times the cone's cosine-weighted solid angle
  const float halfAngle = 0.25f;
  const float s = std::sin(halfAngle);
  pass &= luminance(
      d,
      "directional radiance, cone of 0.5 rad",
      {makeLight(d,
          "directional",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "direction", tilted);
            anari::setParameter(d, l, "radiance", 10.f);
            anari::setParameter(d, l, "angularDiameter", 2.f * halfAngle);
          })},
      [&](float3) { return 10.f * PI * s * s * 0.8f; },
      64,
      0.02f,
      0.002f);
  return pass;
}

// A mirror over the camera's view (z = 0, [-1, 1]^2): smooth white metal
anari::Surface makeMirror(anari::Device d)
{
  const std::vector<float3> v = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);

  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(1.f));
  anari::setParameter(d, mat, "metallic", 1.f);
  anari::setParameter(d, mat, "roughness", 0.f);
  anari::commitParameters(d, mat);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

// A mirror facing the sun: a delta directional light
// (angularDiameter 0) is found only by light sampling, which a mirror's
// delta lobe doesn't see, so the mirror reflects none of it (Typhoon's rays
// gather it within about 0.26 degrees of its direction, a radius, so specular
// paths show a disc of its irradiance as radiance); a sun of
// angularDiameter above 0 is a cone of radiance, which the mirror reflects
bool testMirroredSun(anari::Device d)
{
  bool pass = true;
  // every camera ray reflects straight up (orthographic, looking down)
  auto mirrored = [&](float tilt, float angularDiameter) {
    auto world = makeWorld(d,
        {makeMirror(d)},
        {makeLight(d, "directional", [=](anari::Device d, anari::Light l) {
          anari::setParameter(d,
              l,
              "direction",
              float3(std::sin(tilt), 0.f, -std::cos(tilt)));
          anari::setParameter(d, l, "irradiance", 2.f);
          anari::setParameter(d, l, "angularDiameter", angularDiameter);
        })});
    auto image = render(d, world, 4);
    anari::release(d, world);
    return image;
  };
  auto every = [](const std::vector<float4> &image, float3 c, float tol) {
    if (image.empty())
      return false;
    for (const auto &p : image) {
      if (std::abs(p.x - c.x) > tol * std::max(c.x, 1.f)
          || std::abs(p.y - c.y) > tol * std::max(c.y, 1.f)
          || std::abs(p.z - c.z) > tol * std::max(c.z, 1.f) || p.w != 1.f) {
        std::printf("  (%g, %g, %g, %g), expected (%g, %g, %g, 1)\n",
            p.x,
            p.y,
            p.z,
            p.w,
            c.x,
            c.y,
            c.z);
        return false;
      }
    }
    return true;
  };
  pass &= report("a mirror facing a delta sun reflects none of it",
      every(mirrored(0.f, 0.f), float3(0.f), 0.f));
  pass &= report("... nor where the sun is 0.2 degrees off the reflection",
      every(mirrored(0.2f * PI / 180.f, 0.f), float3(0.f), 0.f));
  // a cone of half angle a: radiance irradiance / (pi sin^2 a)
  const float a = 0.25f;
  const float L = 2.f / (PI * std::sin(a) * std::sin(a));
  pass &= report(
      "a mirror facing a sun of angularDiameter 0.5 rad reflects its radiance",
      every(mirrored(0.f, 2.f * a), L * LIGHT_COLOR, 1e-4f));
  return pass;
}

bool testPoint(anari::Device d)
{
  bool pass = true;
  const float3 pos = {0.3f, -0.2f, 1.5f};

  pass &= luminance(
      d,
      "point intensity, delta",
      {makeLight(d,
          "point",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "intensity", 3.f);
            // intensity wins over power and radiance
            anari::setParameter(d, l, "power", 100.f);
            anari::setParameter(d, l, "radiance", 100.f);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 3.f); },
      64,
      5e-3f,
      1e-3f);

  pass &= luminance(
      d,
      "point power, delta",
      {makeLight(d,
          "point",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "power", 30.f);
            anari::setParameter(d, l, "radiance", 100.f);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 30.f / (4.f * PI)); },
      64,
      5e-3f,
      1e-3f);

  // a sphere of intensity I lights a floor it is wholly above as a point
  // of the same intensity does
  pass &= luminance(
      d,
      "point intensity, sphere of radius 0.5",
      {makeLight(d,
          "point",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "intensity", 3.f);
            anari::setParameter(d, l, "radius", 0.5f);
            anari::setParameter(d, l, "visible", false);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 3.f); },
      64,
      0.03f,
      0.003f);

  pass &= luminance(
      d,
      "point power, sphere of radius 0.5",
      {makeLight(d,
          "point",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "power", 30.f);
            anari::setParameter(d, l, "radius", 0.5f);
            anari::setParameter(d, l, "visible", false);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 30.f / (4.f * PI)); },
      64,
      0.03f,
      0.003f);

  // radiance L: intensity L pi r^2
  pass &= luminance(
      d,
      "point radiance, sphere of radius 0.5",
      {makeLight(d,
          "point",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "radiance", 4.f);
            anari::setParameter(d, l, "radius", 0.5f);
            anari::setParameter(d, l, "visible", false);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 4.f * PI * 0.25f); },
      64,
      0.03f,
      0.003f);
  return pass;
}

// A spot of intensity I lights the floor as a point of intensity I times
// its cone weight; 'power' spreads over the spec's cosine-weighted integral
// of the cone
bool testSpot(anari::Device d)
{
  bool pass = true;
  const float3 pos = {0.2f, -0.1f, 1.5f};
  const float3 tilted = linalg::normalize(float3(0.3f, 0.1f, -1.f));
  const float opening = 1.2f;
  const float falloff = 0.2f;
  auto spot = [&](float I) {
    return [=](float3 p) {
      const float3 e = linalg::normalize(p - pos);
      return pointIrradiance(p, pos, I)
          * coneWeight(angleBetween(e, tilted), opening, falloff);
    };
  };
  // the cone's edge crosses pixels: they need samples to average it, and
  // pixels in its fringe, where the light falls to nothing, are compared
  // against 5% of the brightest (each test's largest error is printed on
  // failure; these were 2-3x below the tolerances when written)
  const Check check{0.03f, 2e-3f, 0.05f};

  pass &= luminance(d,
      "spot intensity",
      {makeLight(d,
          "spot",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "direction", tilted);
            anari::setParameter(d, l, "openingAngle", opening);
            anari::setParameter(d, l, "falloffAngle", falloff);
            anari::setParameter(d, l, "intensity", 3.f);
            // intensity wins over power
            anari::setParameter(d, l, "power", 100.f);
          })},
      spot(3.f),
      256,
      check);

  const double W = solidAngleIntegral(
      [&](double g) {
        return coneWeight(float(g), opening, falloff) * std::cos(g);
      },
      PI / 2.0);
  pass &= luminance(d,
      "spot power",
      {makeLight(d,
          "spot",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "direction", tilted);
            anari::setParameter(d, l, "openingAngle", opening);
            anari::setParameter(d, l, "falloffAngle", falloff);
            anari::setParameter(d, l, "power", 10.f);
          })},
      spot(float(10.0 / W)),
      256,
      check);

  // the defaults: straight down, opening pi, falloff 0.1, intensity 1; the
  // floor's corners are about 0.75 rad off the axis, inside the core cone
  pass &= luminance(
      d,
      "spot defaults (intensity 1, direction -z, opening pi)",
      {makeLight(d,
          "spot",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 1.f); },
      64,
      5e-3f,
      1e-3f);
  return pass;
}

// The quad over the floor: a parallelogram of area 0.48 at z = 1.2 whose
// front (edge2 x edge1) faces down
const float3 QUAD_POS = {-0.5f, -0.3f, 1.2f};
const float3 QUAD_EDGE1 = {0.8f, 0.f, 0.f};
const float3 QUAD_EDGE2 = {0.3f, 0.6f, 0.f};
const float QUAD_AREA = 0.48f;

std::vector<float3> quadCorners(
    float3 pos, float3 e1, float3 e2, const mat4 &xfm = mat4(linalg::identity))
{
  std::vector<float3> v = {pos, pos + e1, pos + e1 + e2, pos + e2};
  for (auto &c : v)
    c = linalg::mul(xfm, float4(c, 1.f)).xyz();
  return v;
}

anari::Light makeQuadLight(anari::Device d,
    const char *side,
    const SetParams &brightness,
    bool flipped = false)
{
  return makeLight(d, "quad", [&](anari::Device d, anari::Light l) {
    anari::setParameter(d, l, "position", QUAD_POS);
    // swapping the edges turns the front up
    anari::setParameter(d, l, "edge1", flipped ? QUAD_EDGE2 : QUAD_EDGE1);
    anari::setParameter(d, l, "edge2", flipped ? QUAD_EDGE1 : QUAD_EDGE2);
    if (side)
      anari::setParameter(d, l, "side", side);
    anari::setParameter(d, l, "visible", false);
    brightness(d, l);
  });
}

bool testQuad(anari::Device d)
{
  bool pass = true;
  const auto corners = quadCorners(QUAD_POS, QUAD_EDGE1, QUAD_EDGE2);
  auto quad = [&](float L) {
    return [=](float3 p) { return polygonIrradiance(p, corners, L); };
  };
  const Check check{0.03f, 3e-3f};

  pass &= luminance(d,
      "quad intensity",
      {makeQuadLight(d,
          nullptr,
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "intensity", 2.f);
            // intensity wins over power and radiance
            anari::setParameter(d, l, "power", 100.f);
            anari::setParameter(d, l, "radiance", 100.f);
          })},
      quad(2.f / QUAD_AREA),
      256,
      check);

  pass &= luminance(d,
      "quad power (one side: pi A L)",
      {makeQuadLight(d,
          "front",
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "power", 5.f);
            anari::setParameter(d, l, "radiance", 100.f);
          })},
      quad(5.f / (PI * QUAD_AREA)),
      256,
      check);

  pass &= luminance(d,
      "quad radiance",
      {makeQuadLight(d,
          nullptr,
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "radiance", 3.f);
          })},
      quad(3.f),
      256,
      check);

  pass &= luminance(d,
      "quad 'back' lights the floor with its front up",
      {makeQuadLight(
          d,
          "back",
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "radiance", 3.f);
          },
          true)},
      quad(3.f),
      256,
      check);

  pass &= luminance(d,
      "quad 'both' power (two sides: 2 pi A L)",
      {makeQuadLight(d,
          "both",
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "power", 5.f);
          })},
      quad(5.f / (2.f * PI * QUAD_AREA)),
      256,
      check);

  {
    auto world = makeWorld(d,
        {makeFloor(d)},
        {makeQuadLight(
            d,
            "front",
            [](anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "radiance", 3.f);
            },
            true)});
    const auto image = render(d, world, 4);
    anari::release(d, world);
    bool dark = !image.empty();
    for (const auto &p : image)
      dark = dark && p.x == 0.f && p.y == 0.f && p.z == 0.f;
    pass &= report("quad 'front' facing up leaves the floor dark", dark);
  }

  // an instance of scale 2 maps the edges (vectors), so the area grows by
  // 4 and 'intensity' spreads over it
  const mat4 xfm =
      linalg::mul(linalg::translation_matrix(float3(0.f, 0.f, 0.4f)),
          linalg::scaling_matrix(float3(2.f, 2.f, 1.f)));
  const auto scaled = quadCorners(QUAD_POS, QUAD_EDGE1, QUAD_EDGE2, xfm);
  pass &= luminance(
      d,
      "instanced quad scaled: intensity over the scaled area",
      {makeQuadLight(d,
          nullptr,
          [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "intensity", 2.f);
          })},
      [&](float3 p) {
        return polygonIrradiance(p, scaled, 2.f / (4.f * QUAD_AREA));
      },
      256,
      check,
      true,
      xfm);
  return pass;
}

// The ring over the floor: at z = 1, facing down
const float3 RING_POS = {0.1f, 0.2f, 1.f};

bool testRing(anari::Device d)
{
  bool pass = true;
  const float R = 0.6f;
  const float r = 0.3f;
  const float area = PI * (R * R - r * r);
  // a Lambertian annulus parallel to the floor
  auto annulus = [&](float L) {
    return [=](float3 p) {
      const float h = RING_POS.z - p.z;
      const float off =
          linalg::length(float2(p.x - RING_POS.x, p.y - RING_POS.y));
      return diskIrradiance(h, off, R, L) - diskIrradiance(h, off, r, L);
    };
  };
  auto ring = [&](const SetParams &params) {
    return makeLight(d, "ring", [&](anari::Device d, anari::Light l) {
      anari::setParameter(d, l, "position", RING_POS);
      anari::setParameter(d, l, "radius", R);
      anari::setParameter(d, l, "innerRadius", r);
      anari::setParameter(d, l, "visible", false);
      params(d, l);
    });
  };
  const Check check{0.03f, 3e-3f};

  // without a cone (falloff 0 at the default opening pi) the ring is a
  // Lambertian emitter
  pass &= luminance(d,
      "ring intensity",
      {ring([](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "falloffAngle", 0.f);
        anari::setParameter(d, l, "intensity", 2.f);
        anari::setParameter(d, l, "power", 100.f);
        anari::setParameter(d, l, "radiance", 100.f);
      })},
      annulus(2.f / area),
      256,
      check);

  pass &= luminance(d,
      "ring power (pi A L)",
      {ring([](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "falloffAngle", 0.f);
        anari::setParameter(d, l, "power", 5.f);
        anari::setParameter(d, l, "radiance", 100.f);
      })},
      annulus(5.f / (PI * area)),
      256,
      check);

  pass &= luminance(d,
      "ring radiance",
      {ring([](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "falloffAngle", 0.f);
        anari::setParameter(d, l, "radiance", 3.f);
      })},
      annulus(3.f),
      256,
      check);

  // a cone, tilted: the radiance L s(gamma) of a ring of intensity I has
  // L = I / A, and 'power' spreads over the integral of s(gamma) cos(gamma).
  // Light sampling ignores the cone, so where it cuts through the ring seen
  // from a point, few samples carry the light; a smaller ring keeps that
  // noise down.
  const float coneR = 0.25f;
  const float coneRi = 0.1f;
  const float coneArea = PI * (coneR * coneR - coneRi * coneRi);
  const float3 dir = linalg::normalize(float3(0.4f, 0.f, -1.f));
  const float opening = 1.4f;
  const float falloff = 0.4f;
  const float3 x = linalg::normalize(linalg::cross(dir, float3(0.f, 1.f, 0.f)));
  const float3 y = linalg::cross(dir, x);
  constexpr int N = 48;
  auto coned = [&](float L) {
    return [=](float3 p) {
      return surfaceIrradiance(
          p,
          N,
          1.f,
          [&](int i, int j) {
            // polar patches: radius i, angle j
            const float rho = coneRi + (coneR - coneRi) * (i + 0.5f) / N;
            const float phi = 2.f * PI * (j + 0.5f) / N;
            return RING_POS + x * (rho * std::cos(phi))
                + y * (rho * std::sin(phi));
          },
          [&](float3 S, float3 e) {
            const float rho = linalg::length(S - RING_POS);
            const float dA = rho * (coneR - coneRi) / N * 2.f * PI / N;
            const float cosGamma = linalg::dot(e, dir);
            return cosGamma > 0.f ? L * dA * cosGamma
                    * coneWeight(angleBetween(e, dir), opening, falloff)
                                  : 0.f;
          });
    };
  };
  auto conedRing = [&](const SetParams &params) {
    return ring([&](anari::Device d, anari::Light l) {
      anari::setParameter(d, l, "direction", dir);
      anari::setParameter(d, l, "openingAngle", opening);
      anari::setParameter(d, l, "falloffAngle", falloff);
      anari::setParameter(d, l, "radius", coneR);
      anari::setParameter(d, l, "innerRadius", coneRi);
      params(d, l);
    });
  };
  // area sampling ignores the cone, so where it cuts through the ring the
  // pixels are noisy (3.5% at most at 1024 samples); the expected
  // quadrature is costly, so 4 x 4 points per pixel average it
  const Check conedCheck{0.06f, 3e-3f, 0.05f, 4};
  pass &= luminance(d,
      "ring intensity, tilted cone",
      {conedRing([](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "intensity", 2.f);
      })},
      coned(2.f / coneArea),
      1024,
      conedCheck);
  const double W = solidAngleIntegral(
      [&](double g) {
        return coneWeight(float(g), opening, falloff) * std::cos(g);
      },
      PI / 2.0);
  pass &= luminance(d,
      "ring power, tilted cone",
      {conedRing([](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "power", 5.f);
      })},
      coned(float(5.0 / W) / coneArea),
      1024,
      conedCheck);

  // radius 0: a delta point of intensity I s(gamma) cos(gamma)
  const float3 pos = {0.2f, -0.1f, 1.5f};
  pass &= luminance(
      d,
      "ring of radius 0 (delta), tilted cone",
      {makeLight(d,
          "ring",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "direction", dir);
            anari::setParameter(d, l, "openingAngle", opening);
            anari::setParameter(d, l, "falloffAngle", falloff);
            anari::setParameter(d, l, "intensity", 3.f);
          })},
      [&](float3 p) {
        const float3 e = linalg::normalize(p - pos);
        const float cosGamma = linalg::dot(e, dir);
        return cosGamma > 0.f ? pointIrradiance(p, pos, 3.f) * cosGamma
                * coneWeight(angleBetween(e, dir), opening, falloff)
                              : 0.f;
      },
      256,
      // the cone's fringe as for the spot
      Check{0.03f, 2e-3f, 0.05f});
  return pass;
}

// 'intensityDistribution' replaces a quad's or ring's Lambertian falloff:
// its intensity towards e is I D(gamma, C), so its radiance I D / (A cos);
// 'power' spreads over the integral of D. A 1D table over gamma, and a 2D
// one brighter towards C = 0 (along edge1 or c0) than the other way.
bool testDistribution(anari::Device d)
{
  bool pass = true;
  const Distribution D1 = {{1.f, 0.9f, 0.5f, 0.2f, 0.f}, 5};
  Distribution D2 = {{}, 3, 4};
  // not symmetric about C = 0, so that C's sense counts
  for (float c : {1.f, 0.8f, 0.2f, 0.4f}) {
    for (float g : {1.f, 0.7f, 0.1f})
      D2.values.push_back(c * g);
  }
  auto setDistribution = [](anari::Device d,
                             anari::Light l,
                             const Distribution &D) {
    if (D.numC == 1) {
      anari::setParameterArray1D(
          d, l, "intensityDistribution", D.values.data(), D.values.size());
    } else {
      anari::setParameterArray2D(
          d, l, "intensityDistribution", D.values.data(), D.numGamma, D.numC);
    }
  };

  const float3 front = {0.f, 0.f, -1.f};
  const float3 c0 = linalg::normalize(QUAD_EDGE1);
  constexpr int N = 24;
  auto quad = [&](const Distribution &D, float I) {
    return [=, &D](float3 p) {
      return surfaceIrradiance(
          p,
          N,
          QUAD_AREA / (N * N),
          [&](int i, int j) {
            return QUAD_POS + QUAD_EDGE1 * ((i + 0.5f) / N)
                + QUAD_EDGE2 * ((j + 0.5f) / N);
          },
          [&](float3, float3 e) {
            // per unit area: the intensity over the area
            return distributedIntensity(D, front, c0, e, I) / QUAD_AREA;
          });
    };
  };
  // a quadrature of 24 x 24 patches per point, averaged over 4 x 4 points
  // per pixel; the largest errors were near 1% when written
  const Check check{0.04f, 3e-3f, 0.02f, 4};
  const Distribution *distributions[] = {&D1, &D2};
  for (const Distribution *D : distributions) {
    const std::string dims = D->numC == 1 ? "1D" : "2D";
    pass &= luminance(d,
        ("quad intensity, " + dims + " intensityDistribution").c_str(),
        {makeQuadLight(d,
            nullptr,
            [&](anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "intensity", 2.f);
              setDistribution(d, l, *D);
            })},
        quad(*D, 2.f),
        256,
        check);
    const double W = solidAngleIntegral(
        [&](double g) { return D->mean(float(g)); }, PI / 2.0);
    pass &= luminance(d,
        ("quad power, " + dims + " intensityDistribution").c_str(),
        {makeQuadLight(d,
            nullptr,
            [&](anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "power", 5.f);
              setDistribution(d, l, *D);
            })},
        quad(*D, float(5.0 / W)),
        256,
        check);
  }

  // a ring of radius 0 with 'c0' along +y: a delta point of intensity
  // I D(gamma, C), with C measured from c0 right-handed about 'direction'
  const float3 pos = {0.2f, -0.1f, 1.2f};
  const float3 ringC0 = {0.f, 1.f, 0.f};
  pass &= luminance(
      d,
      "ring of radius 0, 2D intensityDistribution and c0",
      {makeLight(d,
          "ring",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", pos);
            anari::setParameter(d, l, "c0", ringC0);
            anari::setParameter(d, l, "falloffAngle", 0.f);
            anari::setParameter(d, l, "intensity", 3.f);
            setDistribution(d, l, D2);
          })},
      [&](float3 p) {
        const float3 e = linalg::normalize(p - pos);
        return distributedIntensity(D2, front, ringC0, e, 3.f) * (-e.z)
            / linalg::length2(p - pos);
      },
      64,
      // a delta light: only the pixel averaging of the distribution's kinks
      // (largest error 0.5% when written)
      Check{0.01f, 1e-3f});
  return pass;
}

// Instance transforms move lights' positions and turn their directions, but
// don't scale their radius; a light in an instance of several transforms
// is several lights
bool testInstanced(anari::Device d)
{
  bool pass = true;
  // scale 2, then move up by 0.5: (0.1, -0.1, 0.5) goes to (0.2, -0.2, 1.5)
  const mat4 xfm =
      linalg::mul(linalg::translation_matrix(float3(0.f, 0.f, 0.5f)),
          linalg::scaling_matrix(float3(2.f)));
  const float3 pos = {0.2f, -0.2f, 1.5f};
  pass &= luminance(
      d,
      "instanced sphere moved, radius not scaled",
      {makeLight(d,
          "point",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", float3(0.1f, -0.1f, 0.5f));
            anari::setParameter(d, l, "radiance", 4.f);
            anari::setParameter(d, l, "radius", 0.4f);
            anari::setParameter(d, l, "visible", false);
          })},
      [&](float3 p) { return pointIrradiance(p, pos, 4.f * PI * 0.16f); },
      64,
      0.03f,
      0.003f,
      true,
      xfm);

  // turned by 90 degrees about y, (x, y, z) -> (z, y, -x): a direction of
  // (0.8, 0, 0.6) becomes (0.6, 0, -0.8), at cos 0.8 to the floor's normal
  const mat4 rot = linalg::rotation_matrix(
      linalg::rotation_quat(float3(0.f, 1.f, 0.f), PI / 2.f));
  pass &= luminance(
      d,
      "instanced directional turned",
      {makeLight(d,
          "directional",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "direction", float3(0.8f, 0.f, 0.6f));
            anari::setParameter(d, l, "irradiance", 2.f);
          })},
      [](float3) { return 2.f * 0.8f; },
      1,
      1e-4f,
      1e-4f,
      true,
      rot);

  // scale 2, then move up by 0.5: the ring's center moves, (0.05, 0, 0.5)
  // to (0.1, 0, 1.5), but its radii don't scale
  const mat4 ringXfm =
      linalg::mul(linalg::translation_matrix(float3(0.f, 0.f, 0.5f)),
          linalg::scaling_matrix(float3(2.f)));
  const float R = 0.5f;
  const float r = 0.2f;
  pass &= luminance(
      d,
      "instanced ring moved, radii not scaled",
      {makeLight(d,
          "ring",
          [&](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", float3(0.05f, 0.f, 0.5f));
            anari::setParameter(d, l, "radius", R);
            anari::setParameter(d, l, "innerRadius", r);
            anari::setParameter(d, l, "falloffAngle", 0.f);
            anari::setParameter(d, l, "radiance", 3.f);
            anari::setParameter(d, l, "visible", false);
          })},
      [&](float3 p) {
        const float h = 1.5f - p.z;
        const float off = linalg::length(float2(p.x - 0.1f, p.y));
        return diskIrradiance(h, off, R, 3.f) - diskIrradiance(h, off, r, 3.f);
      },
      256,
      0.03f,
      3e-3f,
      true,
      ringXfm);
  return pass;
}

// Several lights add up, and a surface between the floor and a light
// shadows the floor
bool testShadowsAndSums(anari::Device d)
{
  bool pass = true;
  auto dirLight = [&]() {
    return makeLight(d, "directional", [](anari::Device d, anari::Light l) {
      // from up +x at 45 degrees
      anari::setParameter(
          d, l, "direction", linalg::normalize(float3(-1.f, 0.f, -1.f)));
      anari::setParameter(d, l, "irradiance", 1.f);
    });
  };
  const float3 pos = {-0.5f, 0.f, 1.f};
  auto pointLight = [&]() {
    return makeLight(d, "point", [&](anari::Device d, anari::Light l) {
      anari::setParameter(d, l, "position", pos);
      anari::setParameter(d, l, "intensity", 1.f);
    });
  };
  const float c = std::sqrt(0.5f);

  pass &= luminance(
      d,
      "two lights add up",
      {dirLight(), pointLight()},
      [&](float3 p) { return c + pointIrradiance(p, pos, 1.f); },
      64,
      5e-3f,
      1e-3f);

  // a wall z in [1, 2] at x in [1.2, 2] (out of view) shadows x in [0.2, 1]
  // from the directional light
  auto world = makeWorld(d,
      {makeFloor(d),
          makeQuad(d,
              {{1.2f, -2.f, 1.f},
                  {2.f, -2.f, 1.f},
                  {2.f, 2.f, 1.f},
                  {1.2f, 2.f, 1.f}},
              float3(1.f))},
      {dirLight()});
  const auto image = render(d, world);
  anari::release(d, world);
  bool shadowed = !image.empty();
  for (uint32_t y = 0; shadowed && y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float3 p = floorPoint(x, y);
      const float4 px = image[size_t(y) * IMAGE_SIZE.x + x];
      const float expected = p.x > 0.25f && p.x < 0.95f ? 0.f
          : p.x < 0.15f                                 ? ALBEDO.x / PI * c
                                                        : -1.f;
      if (expected >= 0.f && std::abs(px.x - expected) > 1e-4f)
        shadowed = false;
    }
  }
  pass &= report("a surface shadows the floor", shadowed);
  return pass;
}

// Camera rays see a 'visible' sphere light's radiance in front of the floor;
// not with 'visible' false, nor delta or directional lights
bool testVisibility(anari::Device d)
{
  bool pass = true;
  const float3 pos = {0.f, 0.f, 1.f};
  auto sphere = [&](bool visible) {
    return makeLight(d, "point", [=](anari::Device d, anari::Light l) {
      anari::setParameter(d, l, "position", pos);
      anari::setParameter(d, l, "radius", 0.4f);
      anari::setParameter(d, l, "radiance", 2.f);
      if (!visible)
        anari::setParameter(d, l, "visible", false);
    });
  };
  const size_t center =
      size_t(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2;

  auto world = makeWorld(d, {makeFloor(d)}, {sphere(true)});
  auto image = render(d, world, 4);
  anari::release(d, world);
  const float3 L = 2.f * LIGHT_COLOR;
  pass &= report("a visible sphere light shows its radiance",
      !image.empty() && std::abs(image[center].x - L.x) < 1e-5f
          && std::abs(image[center].y - L.y) < 1e-5f
          && std::abs(image[center].z - L.z) < 1e-5f && image[center].w == 1.f);
  // pixels away from it show the lit floor
  pass &= report("around it the floor shows",
      !image.empty() && image[0].x > 0.f && image[0].x < L.x * 0.5f);

  world = makeWorld(d, {makeFloor(d)}, {sphere(false)});
  image = render(d, world, 64);
  anari::release(d, world);
  const float E = 2.f * PI * 0.16f; // straight below at distance 1
  pass &= report("an invisible sphere light shows the floor it lights",
      !image.empty()
          && std::abs(image[center].x - ALBEDO.x / PI * LIGHT_COLOR.x * E)
              < 0.03f * ALBEDO.x / PI * LIGHT_COLOR.x * E);

  // no surfaces: camera rays see only the background
  world = makeWorld(d,
      {},
      {makeLight(d,
           "directional",
           [](anari::Device d, anari::Light l) {
             anari::setParameter(d, l, "direction", float3(0.f, 0.f, -1.f));
             anari::setParameter(d, l, "angularDiameter", 0.5f);
           }),
          makeLight(d,
              "point",
              [](anari::Device d, anari::Light l) {
                anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
              }),
          makeLight(d,
              "spot",
              [](anari::Device d, anari::Light l) {
                anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
                anari::setParameter(d, l, "direction", float3(0.f, 0.f, 1.f));
              }),
          makeLight(d, "ring", [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
            anari::setParameter(d, l, "direction", float3(0.f, 0.f, 1.f));
          })});
  image = render(d, world);
  anari::release(d, world);
  bool background = !image.empty();
  for (const auto &p : image) {
    background = background && p.x == BACKGROUND.x && p.y == BACKGROUND.y
        && p.z == BACKGROUND.z && p.w == BACKGROUND.w;
  }
  pass &=
      report("directional lights and delta points, spots and rings aren't seen",
          background);

  // a visible quad or ring shows its radiance from the side it emits to,
  // and is black from the other (Typhoon's light geometry), hiding the
  // floor; invisible, the floor it lights shows through it
  auto quadImage = [&](bool frontUp, bool visible) {
    auto world = makeWorld(d,
        {makeFloor(d)},
        {makeQuadLight(
            d,
            nullptr,
            [=](anari::Device d, anari::Light l) {
              anari::setParameter(d, l, "radiance", 2.f);
              anari::setParameter(d, l, "visible", visible);
            },
            frontUp)});
    auto image = render(d, world, 4);
    anari::release(d, world);
    return image;
  };
  auto shows = [&](const std::vector<float4> &image, float3 c) {
    return !image.empty() && std::abs(image[center].x - c.x) < 1e-5f
        && std::abs(image[center].y - c.y) < 1e-5f
        && std::abs(image[center].z - c.z) < 1e-5f && image[center].w == 1.f;
  };
  pass &= report("a visible quad shows its radiance from its front",
      shows(quadImage(true, true), L));
  pass &= report("a visible quad is black from its back",
      shows(quadImage(false, true), float3(0.f)));
  image = quadImage(false, false);
  pass &= report("an invisible quad shows the floor it lights",
      !image.empty() && image[center].x > 0.f);

  world = makeWorld(d,
      {makeFloor(d)},
      {makeLight(d, "ring", [](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
        anari::setParameter(d, l, "direction", float3(0.f, 0.f, 1.f));
        anari::setParameter(d, l, "radius", 0.4f);
        anari::setParameter(d, l, "radiance", 2.f);
      })});
  image = render(d, world, 4);
  anari::release(d, world);
  pass &= report("a visible ring shows its radiance", shows(image, L));
  return pass;
}

// 'radiance' without a size emits nothing, with a warning
bool testRadianceWithoutSize(anari::Device d)
{
  g_warnings.clear();
  auto world = makeWorld(d,
      {makeFloor(d)},
      {makeLight(d,
           "directional",
           [](anari::Device d, anari::Light l) {
             anari::setParameter(d, l, "radiance", 5.f);
           }),
          makeLight(d, "point", [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
            anari::setParameter(d, l, "radiance", 5.f);
          })});
  const auto image = render(d, world);
  anari::release(d, world);
  bool black = !image.empty();
  for (const auto &p : image)
    black = black && p.x == 0.f && p.y == 0.f && p.z == 0.f;
  bool pass = report("radiance without a size emits nothing", black);
  int warnings = 0;
  for (const auto &w : g_warnings)
    warnings += w.find("emits nothing") != std::string::npos;
  pass &= report(
      "and warns once per light", warnings == 2 && g_warnings.size() == 2);
  g_warnings.clear();
  return pass;
}

// What the spot, quad and ring lights warn about: one warning each, naming
// the problem
bool testWarnings(anari::Device d)
{
  bool pass = true;
  auto warns = [&](const char *name,
                   const char *subtype,
                   const SetParams &params,
                   const char *expected) {
    g_warnings.clear();
    auto world = makeWorld(d, {makeFloor(d)}, {makeLight(d, subtype, params)});
    render(d, world);
    anari::release(d, world);
    const bool ok = g_warnings.size() == 1
        && g_warnings[0].find(expected) != std::string::npos;
    g_warnings.clear();
    return report(name, ok);
  };

  pass &= warns(
      "ring 'radiance' without a radius warns",
      "ring",
      [](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "radiance", 5.f);
      },
      "emits nothing");
  pass &= warns(
      "ring 'innerRadius' not below 'radius' warns",
      "ring",
      [](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "radius", 0.2f);
        anari::setParameter(d, l, "innerRadius", 0.3f);
      },
      "innerRadius");
  pass &= warns(
      "spot zero 'direction' warns",
      "spot",
      [](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "direction", float3(0.f));
      },
      "invalid");
  pass &= warns(
      "quad parallel edges warn",
      "quad",
      [](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "edge2", float3(2.f, 0.f, 0.f));
      },
      "no area");
  pass &= warns(
      "quad unknown 'side' warns",
      "quad",
      [](anari::Device d, anari::Light l) {
        anari::setParameter(d, l, "side", "sideways");
      },
      "'side'");
  pass &= warns(
      "intensityDistribution of one value warns",
      "quad",
      [](anari::Device d, anari::Light l) {
        const float v = 1.f;
        anari::setParameterArray1D(d, l, "intensityDistribution", &v, 1);
      },
      "two values");
  pass &= warns(
      "intensityDistribution of another element type warns",
      "ring",
      [](anari::Device d, anari::Light l) {
        const uint8_t v[] = {1, 2, 3};
        anari::setParameterArray1D(d, l, "intensityDistribution", v, 3);
      },
      "FLOAT32");
  pass &= warns(
      "negative intensityDistribution values warn",
      "ring",
      [](anari::Device d, anari::Light l) {
        const float v[] = {1.f, -2.f, 3.f};
        anari::setParameterArray1D(d, l, "intensityDistribution", v, 3);
      },
      "negative");
  return pass;
}

// Committing a light changes what the next render of its world shows
bool testLightChange(anari::Device d)
{
  auto light = makeLight(d, "directional", [](anari::Device d, anari::Light l) {
    anari::setParameter(d, l, "irradiance", 1.f);
  });
  anari::retain(d, light);
  auto world = makeWorld(d, {makeFloor(d)}, {light});
  bool pass = matchesIrradiance(
      "a light lights its world",
      render(d, world),
      [](float3) { return 1.f; },
      1e-5f,
      1e-5f);
  anari::setParameter(d, light, "irradiance", 3.f);
  anari::commitParameters(d, light);
  pass &= matchesIrradiance(
      "a light's commit changes the next render",
      render(d, world),
      [](float3) { return 3.f; },
      1e-5f,
      1e-5f);
  anari::release(d, light);
  anari::release(d, world);
  return pass;
}

// The 'preview' renderer ignores lights: the same image with and without
// (the hdri, which its misses show, is test_hdri.cpp's)
bool testDefaultRendererIgnoresLights(anari::Device d)
{
  auto lit = makeWorld(d,
      {makeFloor(d)},
      {makeLight(d,
           "point",
           [](anari::Device d, anari::Light l) {
             anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
             anari::setParameter(d, l, "radius", 0.4f);
           }),
          makeLight(d,
              "spot",
              [](anari::Device d, anari::Light l) {
                anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
              }),
          makeQuadLight(d,
              nullptr,
              [](anari::Device d, anari::Light l) {
                anari::setParameter(d, l, "visible", true);
              }),
          makeLight(d, "ring", [](anari::Device d, anari::Light l) {
            anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
            anari::setParameter(d, l, "radius", 0.4f);
          })});
  auto unlit = makeWorld(d, {makeFloor(d)}, {});
  const auto a = render(d, lit, 1, "default");
  const auto b = render(d, unlit, 1, "default");
  anari::release(d, lit);
  anari::release(d, unlit);
  return report("the preview renderer ignores lights",
      !a.empty() && a.size() == b.size()
          && std::memcmp(a.data(), b.data(), a.size() * sizeof(float4)) == 0);
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
  pass &= testDirectional(d);
  pass &= testMirroredSun(d);
  pass &= testPoint(d);
  pass &= testSpot(d);
  pass &= testQuad(d);
  pass &= testRing(d);
  pass &= testDistribution(d);
  pass &= testInstanced(d);
  pass &= testShadowsAndSums(d);
  pass &= testVisibility(d);
  pass &= testLightChange(d);
  pass &= testDefaultRendererIgnoresLights(d);
  pass &= report("no warnings", g_warnings.empty());
  pass &= testRadianceWithoutSize(d);
  pass &= testWarnings(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors", g_errors == 0);
  return pass ? 0 : 1;
}
