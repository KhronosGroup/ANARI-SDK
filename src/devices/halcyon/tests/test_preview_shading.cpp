// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'preview' renderer's default mode, which shades surfaces as OpenUSD
// Storm shades a UsdPreviewSurface (renderer/preview/PreviewShading.isph):
// a floor facing an orthographic camera, compared pixel by pixel with a C++
// reference of Storm's model (Lambert diffuse plus GGX specular with
// Schlick's Fresnel, and the dome-light term with Karis' fit for the
// uniform ambient light), and where the model reduces to it (ior 1,
// roughness 1, metallic 0: F0 = 0), with closed forms: a headlight at normal
// incidence and a directional light give albedo / pi times the irradiance.
//
// Checked: the headlight and the ambient fill 'eyeLightBlendRatio' splits
// 'ambientRadiance' x 'ambientColor' into, the headlight dropped in a world
// with lights; each light as Storm lights it, in halcyon's units
// (light/Light.h): directional (also a cone), point (also power and a
// sphere), spot (its cone), quad and ring (a point at the center with the
// Lambertian cosine and the sides that emit); an hdri's mean radiance added
// to the ambient light; physicallyBased's metallic, roughness, ior and
// emissive (also from an attribute) and other materials as matte; back faces
// lit as front faces; colors above 1 not clamped; ambient occlusion
// darkening only the ambient light; and visible lights (sphere, quad, ring)
// shown with their radiance, a side that doesn't emit black, hidden behind
// surfaces, without changing the depth and id channels.

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
const float3 ALBEDO = {0.8f, 0.6f, 0.4f};
const float3 LIGHT_COLOR = {1.f, 0.5f, 0.75f};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
// the camera looks down -z from z = 3; the floor is z = 0
const float3 E = {0.f, 0.f, 1.f};
const float3 N = {0.f, 0.f, 1.f};
constexpr float CAMERA_Z = 3.f;

int g_errors = 0;
// warnings other than the missing material's, which a surface without one
// expects
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
  } else if (severity == ANARI_SEVERITY_WARNING
      && !std::strstr(message, "material")) {
    std::fprintf(stderr, "[ANARI WARNING] %s\n", message);
    g_warnings++;
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Storm's model (the reference) /////////////////////////////////////////////

constexpr float EPSILON = 0.001f;

struct Surface
{
  float3 base{ALBEDO};
  float metallic{0.f};
  float roughness{1.f};
  float ior{1.5f};
  float3 emissive{0.f};
};

// A light as the shading sees it: the unit direction towards it and the
// irradiance it gives a surface facing it
struct Incident
{
  float3 L;
  float3 irradiance;
};

float3 lerp3(float3 a, float3 b, float t)
{
  return a + (b - a) * t;
}

// The light 's' reflects towards E from 'lights' (and what it emits), and
// from the uniform ambient light 'ambient'
float3 storm(const Surface &s,
    const std::vector<Incident> &lights,
    float3 ambient,
    float occlusion = 1.f)
{
  const float NdotE = std::max(linalg::dot(N, E), 0.f);
  const float roughness = std::max(s.roughness, EPSILON);
  const float R = (1.f - s.ior) / (1.f + s.ior);
  const float3 specColor = lerp3(float3(1.f), s.base, s.metallic);
  const float3 F0 = lerp3(R * R * specColor, specColor, s.metallic);
  const float3 F90 = specColor;
  const float3 diffuseColor = s.base * (1.f - s.metallic);

  float3 direct = s.emissive;
  for (const Incident &l : lights) {
    const float NdotL = linalg::dot(N, l.L);
    if (NdotL <= 0.f)
      continue;
    const float3 H = linalg::normalize(E + l.L);
    const float NdotH = std::max(linalg::dot(N, H), 0.f);
    const float EdotH = std::max(linalg::dot(E, H), 0.f);
    const float x = std::max(0.f, 1.f - EdotH);
    const float3 F = lerp3(F0, F90, x * x * x * x * x);
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float denom = NdotH * NdotH * (alpha2 - 1.f) + 1.f;
    const float D = (alpha2 + EPSILON) / (PI * denom * denom);
    const float k = roughness * roughness * 0.5f;
    const float G =
        NdotE / (NdotE * (1.f - k) + k) * NdotL / (NdotL * (1.f - k) + k);
    const float3 specular = F * (G * D / (4.f * NdotL * NdotE + EPSILON));
    const float3 diffuse = diffuseColor * (float3(1.f) - F) / PI;
    direct += NdotL * (diffuse + specular) * l.irradiance;
  }

  // Karis' fit of the split-sum environment BRDF
  const float4 r = roughness * float4(-1.f, -0.0275f, -0.572f, 0.022f)
      + float4(1.f, 0.0425f, 1.04f, -0.04f);
  const float a004 = std::min(r.x * r.x, std::exp2(-9.28f * NdotE)) * r.x + r.y;
  const float envX = -1.04f * a004 + r.z;
  const float envY = 1.04f * a004 + r.w;
  const float3 amb =
      ambient * (diffuseColor * (float3(1.f) - F0) + F0 * envX + float3(envY));
  return direct + occlusion * amb;
}

// Scene /////////////////////////////////////////////////////////////////////

using SetObject = std::function<void(anari::Device, anari::Object)>;

// A light of LIGHT_COLOR that 'set' configures; not 'visible' unless 'set'
// says so, so the camera doesn't see a sphere, quad or ring
anari::Light makeLight(
    anari::Device d, const char *subtype, const SetObject &set)
{
  auto light = anari::newObject<anari::Light>(d, subtype);
  anari::setParameter(d, light, "color", LIGHT_COLOR);
  anari::setParameter(d, light, "visible", false);
  set(d, light);
  anari::commitParameters(d, light);
  return light;
}

anari::Material matte(anari::Device d, float3 color = ALBEDO)
{
  auto mat = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, mat, "color", color);
  anari::commitParameters(d, mat);
  return mat;
}

anari::Material physicallyBased(anari::Device d, const SetObject &set)
{
  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", ALBEDO);
  set(d, mat);
  anari::commitParameters(d, mat);
  return mat;
}

// A quad with corners 'v' (counter-clockwise seen from its front) and
// material 'mat' (taking over the reference); 'set' sets more geometry
// parameters
anari::Surface makeQuad(anari::Device d,
    const std::vector<float3> &v,
    anari::Material mat,
    const SetObject &set = nullptr)
{
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  if (set)
    set(d, geom);
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

const std::vector<float3> FLOOR = {
    {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};

// The floor z = 0 over the camera's view, facing it
anari::Surface makeFloor(anari::Device d, anari::Material mat)
{
  return makeQuad(d, FLOOR, mat);
}

anari::Surface makeFloor(anari::Device d)
{
  return makeFloor(d, matte(d));
}

// A world of 'surfaces' and 'lights' (taking over the references)
anari::World makeWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Light> lights = {})
{
  auto world = anari::newObject<anari::World>(d);
  if (!surfaces.empty()) {
    anari::setParameterArray1D(
        d, world, "surface", surfaces.data(), surfaces.size());
  }
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto l : lights)
    anari::release(d, l);
  return world;
}

struct Image
{
  std::vector<float4> color;
  std::vector<float> depth;
  std::vector<uint32_t> primId;
};

using SetRenderer = std::function<void(anari::Device, anari::Renderer)>;

// Renders 'world' (released) with the 'preview' renderer in its default
// mode, without ambient occlusion unless 'setRenderer' turns it on,
// through an orthographic camera at z = 3 looking down -z at [-1, 1]^2
Image render(anari::Device d,
    anari::World world,
    const SetRenderer &setRenderer = nullptr)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "ambientOcclusion", false);
  if (setRenderer)
    setRenderer(d, renderer);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, CAMERA_Z));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, frame, "channel.primitiveId", ANARI_UINT32);
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setAndReleaseParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);
  Image img;
  const size_t n = size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    img.color.assign(fb.data, fb.data + n);
  anari::unmap(d, frame, "channel.color");
  auto db = anari::map<float>(d, frame, "channel.depth");
  if (db.data)
    img.depth.assign(db.data, db.data + n);
  anari::unmap(d, frame, "channel.depth");
  auto pb = anari::map<uint32_t>(d, frame, "channel.primitiveId");
  if (pb.data)
    img.primId.assign(pb.data, pb.data + n);
  anari::unmap(d, frame, "channel.primitiveId");
  anari::release(d, frame);
  return img;
}

// The floor point at the center of pixel (x, y), which the camera ray
// through the pixel hits
float3 floorPoint(uint32_t x, uint32_t y)
{
  return float3((x + 0.5f) / IMAGE_SIZE.x * 2.f - 1.f,
      (y + 0.5f) / IMAGE_SIZE.y * 2.f - 1.f,
      0.f);
}

std::string str(float3 v)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", v.x, v.y, v.z);
  return buf;
}

// The expected colour at a floor point; NaN components skip the pixel
using Expected = std::function<float3(float3)>;

// Whether every pixel of 'img' is opaque and within 'tolerance' (relative
// to max(1, |value|)) of 'expected' at its floor point
bool matches(const std::string &name,
    const Image &img,
    const Expected &expected,
    float tolerance = 1e-4f)
{
  if (img.color.size() != size_t(IMAGE_SIZE.x) * IMAGE_SIZE.y)
    return report(name + ": no image", false);
  int bad = 0;
  int checked = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float3 want = expected(floorPoint(x, y));
      if (std::isnan(want.x))
        continue;
      checked++;
      const float4 got = img.color[size_t(y) * IMAGE_SIZE.x + x];
      bool ok = std::fabs(got.w - 1.f) < 1e-6f;
      for (int c = 0; c < 3; c++) {
        ok &= std::fabs(got[c] - want[c])
            <= tolerance * std::max(1.f, std::fabs(want[c]));
      }
      if (!ok && bad++ < 3) {
        std::printf("  pixel (%u, %u): got %s alpha %g, expected %s\n",
            x,
            y,
            str(float3(got.x, got.y, got.z)).c_str(),
            got.w,
            str(want).c_str());
      }
    }
  }
  if (bad > 0)
    std::printf("  %d of %d pixels differ\n", bad, checked);
  return report(name, bad == 0 && checked > 0);
}

bool sameImage(const Image &a, const Image &b)
{
  return !a.color.empty() && a.color == b.color && a.depth == b.depth
      && a.primId == b.primId;
}

const float3 SKIP = float3(NAN);

// The headlight's irradiance: pi x 'eyeLightBlendRatio' x 'ambientRadiance'
Incident headlight(float blend, float radiance = 1.f)
{
  return {E, float3(PI * blend * radiance)};
}

// Renderer parameters ///////////////////////////////////////////////////////

bool testRendererLight(anari::Device d)
{
  bool pass = true;
  const Surface s;

  // defaults: eyeLightBlendRatio 0.5, ignoreAmbientLighting (radiance 1)
  pass &= matches("defaults: headlight and ambient fill halve the radiance",
      render(d, makeWorld(d, {makeFloor(d)})),
      [&](float3) { return storm(s, {headlight(0.5f)}, float3(0.5f)); });

  for (float blend : {0.f, 0.25f, 1.f}) {
    pass &= matches("eyeLightBlendRatio " + std::to_string(blend),
        render(d,
            makeWorld(d, {makeFloor(d)}),
            [&](anari::Device d, anari::Renderer r) {
              anari::setParameter(d, r, "eyeLightBlendRatio", blend);
            }),
        [&](float3) {
          return storm(s, {headlight(blend)}, float3(1.f - blend));
        });
  }

  pass &= matches("ambientRadiance and ambientColor",
      render(d,
          makeWorld(d, {makeFloor(d)}),
          [&](anari::Device d, anari::Renderer r) {
            anari::setParameter(d, r, "ignoreAmbientLighting", false);
            anari::setParameter(d, r, "ambientRadiance", 2.f);
            anari::setParameter(d, r, "ambientColor", float3(1.f, 0.5f, 0.25f));
            anari::setParameter(d, r, "eyeLightBlendRatio", 0.25f);
          }),
      [&](float3) {
        // the headlight isn't coloured by ambientColor
        return storm(
            s, {headlight(0.25f, 2.f)}, 0.75f * 2.f * float3(1.f, 0.5f, 0.25f));
      });

  pass &= matches("ignoreAmbientLighting ignores ambientColor",
      render(d,
          makeWorld(d, {makeFloor(d)}),
          [&](anari::Device d, anari::Renderer r) {
            anari::setParameter(d, r, "ambientColor", float3(1.f, 0.5f, 0.25f));
          }),
      [&](float3) { return storm(s, {headlight(0.5f)}, float3(0.5f)); });

  // F0 = 0 (ior 1), roughness 1, metallic 0: a Lambertian, whose headlight
  // at normal incidence gives the base colour
  pass &= matches("headlight at normal incidence on a Lambertian is albedo",
      render(d,
          makeWorld(d,
              {makeFloor(d,
                  physicallyBased(d,
                      [](anari::Device d, anari::Object m) {
                        anari::setParameter(d, m, "metallic", 0.f);
                        anari::setParameter(d, m, "roughness", 1.f);
                        anari::setParameter(d, m, "ior", 1.f);
                      }))}),
          [&](anari::Device d, anari::Renderer r) {
            anari::setParameter(d, r, "eyeLightBlendRatio", 1.f);
          }),
      [&](float3) { return ALBEDO; });
  return pass;
}

// Lights ////////////////////////////////////////////////////////////////////

// A world of the floor and 'light', rendered with eyeLightBlendRatio 'blend'
Image renderLit(anari::Device d, anari::Light light, float blend = 0.5f)
{
  return render(d,
      makeWorld(d, {makeFloor(d)}, {light}),
      [&](anari::Device d, anari::Renderer r) {
        anari::setParameter(d, r, "eyeLightBlendRatio", blend);
      });
}

// The floor's colour lit by 'incident' (or only ambient where it is
// empty) and the ambient fill of eyeLightBlendRatio 0.5, without headlight
float3 litFloor(std::vector<Incident> incident, const Surface &s = {})
{
  return storm(s, incident, float3(0.5f));
}

// A light at 'center' whose intensity towards floor point P is
// 'intensity' x 'profile(e)' (e the unit direction from the light to P)
Expected pointLike(float3 center,
    float3 intensity,
    const std::function<float(float3)> &profile = nullptr,
    const Surface &s = {})
{
  return [=](float3 P) {
    const float3 toLight = center - P;
    const float dist2 = linalg::dot(toLight, toLight);
    const float3 L = toLight / std::sqrt(dist2);
    const float f = profile ? profile(-L) : 1.f;
    if (f <= 0.f)
      return litFloor({}, s);
    return litFloor({{L, intensity * f / dist2}}, s);
  };
}

bool testDirectional(anari::Device d)
{
  bool pass = true;
  const float3 dir = linalg::normalize(float3(0.3f, -0.2f, -1.f));
  auto directional = [&](float angularDiameter) {
    return makeLight(d, "directional", [=](anari::Device d, anari::Object l) {
      anari::setParameter(d, l, "direction", dir);
      anari::setParameter(d, l, "irradiance", 2.f);
      anari::setParameter(d, l, "angularDiameter", angularDiameter);
    });
  };
  const Expected expected = [&](float3) {
    return litFloor({{-dir, 2.f * LIGHT_COLOR}});
  };
  pass &= matches("directional: irradiance, no headlight",
      renderLit(d, directional(0.f)),
      expected);
  // a cone of the same irradiance lights alike
  pass &= matches("directional: a cone of the same irradiance",
      renderLit(d, directional(0.5f)),
      expected);

  // a Lambertian under a sun overhead: albedo / pi x irradiance
  auto overhead =
      makeLight(d, "directional", [](anari::Device d, anari::Object l) {
        anari::setParameter(d, l, "direction", float3(0.f, 0.f, -1.f));
        anari::setParameter(d, l, "irradiance", 3.f);
      });
  pass &= matches("directional: a Lambertian is albedo / pi x irradiance",
      render(d,
          makeWorld(d,
              {makeFloor(d,
                  physicallyBased(d,
                      [](anari::Device d, anari::Object m) {
                        anari::setParameter(d, m, "metallic", 0.f);
                        anari::setParameter(d, m, "ior", 1.f);
                      }))},
              {overhead}),
          [](anari::Device d, anari::Renderer r) {
            anari::setParameter(d, r, "eyeLightBlendRatio", 1.f);
          }),
      [](float3) { return ALBEDO * LIGHT_COLOR * (3.f / PI); });
  return pass;
}

bool testPointAndSpot(anari::Device d)
{
  bool pass = true;
  const float3 center(0.2f, 0.3f, 1.f);
  auto point = [&](const SetObject &set) {
    return makeLight(d, "point", [=](anari::Device d, anari::Object l) {
      anari::setParameter(d, l, "position", center);
      set(d, l);
    });
  };
  const Expected expected = pointLike(center, 4.f * LIGHT_COLOR);
  pass &= matches("point: intensity / distance^2",
      renderLit(d, point([](anari::Device d, anari::Object l) {
        anari::setParameter(d, l, "intensity", 4.f);
      })),
      expected);
  pass &= matches("point: power / (4 pi)",
      renderLit(d, point([](anari::Device d, anari::Object l) {
        anari::setParameter(d, l, "power", 16.f * PI);
      })),
      expected);
  pass &= matches("point: a sphere of the same intensity, from its center",
      renderLit(d, point([](anari::Device d, anari::Object l) {
        anari::setParameter(d, l, "intensity", 4.f);
        anari::setParameter(d, l, "radius", 0.1f);
      })),
      expected);

  // a spot straight down with a hard edge (falloffAngle 0) at 30 degrees
  // off its axis: full intensity inside, none outside; pixels near the edge
  // are skipped
  const float halfAngle = PI / 6.f;
  auto spot = makeLight(d, "spot", [&](anari::Device d, anari::Object l) {
    anari::setParameter(d, l, "position", center);
    anari::setParameter(d, l, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, l, "openingAngle", 2.f * halfAngle);
    anari::setParameter(d, l, "falloffAngle", 0.f);
    anari::setParameter(d, l, "intensity", 4.f);
  });
  const Expected inCone = pointLike(center, 4.f * LIGHT_COLOR);
  pass &= matches("spot: intensity inside its cone, none outside",
      renderLit(d, spot),
      [&](float3 P) {
        const float3 e = linalg::normalize(P - center);
        const float angle = std::acos(-e.z);
        if (std::fabs(angle - halfAngle) < 0.05f)
          return SKIP;
        return angle < halfAngle ? inCone(P) : litFloor({});
      });
  return pass;
}

// A quad light at height 1 over the floor's center, of edges 0.4 along x
// and y, emitting from 'side': its front (edge2 x edge1) faces down
anari::Light quadLight(anari::Device d,
    const char *side,
    const SetObject &set,
    bool visible = false,
    float z = 1.f)
{
  return makeLight(d, "quad", [=](anari::Device d, anari::Object l) {
    anari::setParameter(d, l, "position", float3(-0.2f, -0.2f, z));
    anari::setParameter(d, l, "edge1", float3(0.4f, 0.f, 0.f));
    anari::setParameter(d, l, "edge2", float3(0.f, 0.4f, 0.f));
    anari::setParameter(d, l, "side", side);
    anari::setParameter(d, l, "visible", visible);
    set(d, l);
  });
}

bool testQuadAndRing(anari::Device d)
{
  bool pass = true;
  const float3 center(0.f, 0.f, 1.f);
  // a Lambertian emitter's intensity falls off with the cosine off its
  // normal (down)
  const auto lambertian = [](float3 e) { return std::max(-e.z, 0.f); };
  const Expected expected = pointLike(center, 2.f * LIGHT_COLOR, lambertian);
  const SetObject intensity = [](anari::Device d, anari::Object l) {
    anari::setParameter(d, l, "intensity", 2.f);
  };

  pass &= matches("quad: a point at its center, Lambertian",
      renderLit(d, quadLight(d, "front", intensity)),
      expected);
  pass &= matches("quad: radiance x area",
      renderLit(d,
          quadLight(d,
              "front",
              [](anari::Device d, anari::Object l) {
                anari::setParameter(d, l, "radiance", 2.f / (0.4f * 0.4f));
              })),
      expected);
  pass &= matches("quad: both sides emit as the front",
      renderLit(d, quadLight(d, "both", intensity)),
      expected);
  pass &= matches("quad: the back faces away from the floor",
      renderLit(d, quadLight(d, "back", intensity)),
      [](float3) { return litFloor({}); });

  auto ring = [&](float radius) {
    return makeLight(d, "ring", [=](anari::Device d, anari::Object l) {
      anari::setParameter(d, l, "position", center);
      anari::setParameter(d, l, "direction", float3(0.f, 0.f, -1.f));
      anari::setParameter(d, l, "radius", radius);
      anari::setParameter(d, l, "intensity", 2.f);
    });
  };
  pass &= matches("ring: a point at its center, Lambertian",
      renderLit(d, ring(0.2f)),
      expected);
  pass &= matches("ring of radius 0: the same delta point",
      renderLit(d, ring(0.f)),
      expected);
  return pass;
}

// An hdri of 'width' x 2 texels: the upper row (towards 'up', +z) 'upper',
// the lower 'lower'
anari::Light hdri(
    anari::Device d, float3 upper, float3 lower, float scale, bool visible)
{
  const std::vector<float3> texels = {lower, lower, upper, upper};
  auto light = anari::newObject<anari::Light>(d, "hdri");
  anari::setParameterArray2D(d, light, "radiance", texels.data(), 2, 2);
  anari::setParameter(d, light, "up", float3(0.f, 0.f, 1.f));
  anari::setParameter(d, light, "direction", float3(1.f, 0.f, 0.f));
  anari::setParameter(d, light, "scale", scale);
  anari::setParameter(d, light, "visible", visible);
  anari::commitParameters(d, light);
  return light;
}

bool testHdri(anari::Device d)
{
  bool pass = true;
  // each row covers half the sphere: the mean is the rows' mean
  const float3 upper(1.f, 0.5f, 0.25f);
  const float3 lower(0.2f, 0.2f, 0.2f);
  const float3 mean = 0.5f * (upper + lower);
  for (bool visible : {true, false}) {
    pass &= matches(std::string("hdri: its mean radiance x scale joins the "
                                "ambient light, no headlight")
            + (visible ? "" : " (not visible)"),
        render(d,
            makeWorld(d, {makeFloor(d)}, {hdri(d, upper, lower, 2.f, visible)}),
            [](anari::Device d, anari::Renderer r) {
              anari::setParameter(d, r, "eyeLightBlendRatio", 0.25f);
            }),
        [&](float3) { return storm(Surface{}, {}, 0.75f + 2.f * mean); });
  }
  return pass;
}

// Materials /////////////////////////////////////////////////////////////////

bool testMaterials(anari::Device d)
{
  bool pass = true;
  const float3 center(0.2f, 0.3f, 1.f);
  auto light = [&]() {
    return makeLight(d, "point", [=](anari::Device d, anari::Object l) {
      anari::setParameter(d, l, "position", center);
      anari::setParameter(d, l, "intensity", 4.f);
    });
  };

  Surface pbr;
  pbr.metallic = 0.7f;
  pbr.roughness = 0.3f;
  pbr.ior = 1.8f;
  pbr.emissive = float3(0.1f, 0.2f, 0.3f);
  const SetObject setPbr = [](anari::Device d, anari::Object m) {
    anari::setParameter(d, m, "metallic", 0.7f);
    anari::setParameter(d, m, "roughness", 0.3f);
    anari::setParameter(d, m, "ior", 1.8f);
    anari::setParameter(d, m, "emissive", float3(0.1f, 0.2f, 0.3f));
  };
  const Expected expected = pointLike(center, 4.f * LIGHT_COLOR, nullptr, pbr);
  pass &= matches("physicallyBased: metallic, roughness, ior, emissive",
      render(d,
          makeWorld(d, {makeFloor(d, physicallyBased(d, setPbr))}, {light()})),
      expected);

  // the same from geometry attributes
  const std::vector<float> metallic(4, 0.7f);
  const std::vector<float> roughness(4, 0.3f);
  auto fromAttributes =
      physicallyBased(d, [](anari::Device d, anari::Object m) {
        anari::setParameter(d, m, "metallic", "attribute0");
        anari::setParameter(d, m, "roughness", "attribute1");
        anari::setParameter(d, m, "ior", 1.8f);
        anari::setParameter(d, m, "emissive", float3(0.1f, 0.2f, 0.3f));
      });
  pass &= matches("physicallyBased: metallic and roughness from attributes",
      render(d,
          makeWorld(d,
              {makeQuad(d,
                  FLOOR,
                  fromAttributes,
                  [&](anari::Device d, anari::Object g) {
                    anari::setParameterArray1D(d,
                        g,
                        "vertex.attribute0",
                        metallic.data(),
                        metallic.size());
                    anari::setParameterArray1D(d,
                        g,
                        "vertex.attribute1",
                        roughness.data(),
                        roughness.size());
                  })},
              {light()})),
      expected);

  // a surface without a material is a matte of invalidMaterialColor
  auto bare = anari::newObject<anari::Geometry>(d, "triangle");
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  anari::setParameterArray1D(
      d, bare, "vertex.position", FLOOR.data(), FLOOR.size());
  anari::setParameterArray1D(
      d, bare, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, bare);
  auto bareSurface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, bareSurface, "geometry", bare);
  anari::commitParameters(d, bareSurface);
  Surface invalid;
  invalid.base = float3(1.f, 0.f, 1.f); // the default invalidMaterialColor
  pass &= matches("no material: a matte of invalidMaterialColor",
      render(d, makeWorld(d, {bareSurface}, {light()})),
      pointLike(center, 4.f * LIGHT_COLOR, nullptr, invalid));
  return pass;
}

// Normal maps ///////////////////////////////////////////////////////////////

// The largest difference between the colours of 'a' and 'b' (relative to
// max(1, |value|) per component), infinite if either has no image
float imageDifference(const Image &a, const Image &b)
{
  if (a.color.empty() || a.color.size() != b.color.size())
    return INFINITY;
  float worst = 0.f;
  for (size_t i = 0; i < a.color.size(); i++) {
    for (int c = 0; c < 4; c++) {
      worst = std::max(worst,
          std::fabs(a.color[i][c] - b.color[i][c])
              / std::max(1.f, std::fabs(b.color[i][c])));
    }
  }
  return worst;
}

// Whether 'a' and 'b' are the same image to within 'tolerance'
// (imageDifference())
bool closeImages(
    const std::string &name, const Image &a, const Image &b, float tolerance)
{
  const float worst = imageDifference(a, b);
  if (!(worst <= tolerance))
    std::printf("  largest difference %g\n", worst);
  return report(name, worst <= tolerance);
}

// A normal map of 'subtype' ("image2D" or "transform"); an image2D holds the
// one texel 'texel', stored as is or ('encoded') as texel / 2 + 1/2 that
// outTransform/outOffset decode; 'flipT' makes inTransform map t to 1 - t
anari::Sampler normalMap(anari::Device d,
    const char *subtype,
    float3 texel,
    bool encoded = false,
    bool flipT = false)
{
  auto s = anari::newObject<anari::Sampler>(d, subtype);
  if (std::strcmp(subtype, "image2D") == 0) {
    const float3 stored = encoded ? texel * 0.5f + float3(0.5f) : texel;
    anari::setParameterArray2D(d, s, "image", &stored, 1, 1);
    anari::setParameter(d, s, "inAttribute", "attribute0");
  }
  if (encoded) {
    anari::setParameter(d,
        s,
        "outTransform",
        mat4(float4(2.f, 0.f, 0.f, 0.f),
            float4(0.f, 2.f, 0.f, 0.f),
            float4(0.f, 0.f, 2.f, 0.f),
            float4(0.f, 0.f, 0.f, 1.f)));
    anari::setParameter(d, s, "outOffset", float4(-1.f, -1.f, -1.f, 0.f));
  }
  if (flipT) {
    anari::setParameter(d,
        s,
        "inTransform",
        mat4(float4(1.f, 0.f, 0.f, 0.f),
            float4(0.f, -1.f, 0.f, 0.f),
            float4(0.f, 0.f, 1.f, 0.f),
            float4(0.f, 0.f, 0.f, 1.f)));
    anari::setParameter(d, s, "inOffset", float4(0.f, 1.f, 0.f, 0.f));
  }
  anari::commitParameters(d, s);
  return s;
}

bool testNormalMaps(anari::Device d)
{
  bool pass = true;
  auto light = [&]() {
    return makeLight(d, "point", [](anari::Device d, anari::Object l) {
      anari::setParameter(d, l, "position", float3(0.4f, 0.3f, 1.2f));
      anari::setParameter(d, l, "intensity", 4.f);
    });
  };
  // a glossy physicallyBased material, with normal map 'map' (released) if
  // given
  auto material = [&](anari::Sampler map) {
    return physicallyBased(d, [=](anari::Device d, anari::Object m) {
      anari::setParameter(d, m, "metallic", 0.3f);
      anari::setParameter(d, m, "roughness", 0.4f);
      if (map)
        anari::setAndReleaseParameter(d, m, "normal", map);
    });
  };
  // The floor with texture coordinates in attribute0 (u along +x or, with
  // 'mirrorU', -x; v along +y) and, if 'normal' is nonzero, that vertex
  // normal at every corner
  auto floor = [&](anari::Material mat, float3 normal, bool mirrorU) {
    return makeQuad(d, FLOOR, mat, [=](anari::Device d, anari::Object g) {
      const float u0 = mirrorU ? 1.f : 0.f;
      const float u1 = 1.f - u0;
      const std::vector<float2> uv = {
          {u0, 0.f}, {u1, 0.f}, {u1, 1.f}, {u0, 1.f}};
      anari::setParameterArray1D(
          d, g, "vertex.attribute0", uv.data(), uv.size());
      if (normal != float3(0.f)) {
        const std::vector<float3> n(4, linalg::normalize(normal));
        anari::setParameterArray1D(d, g, "vertex.normal", n.data(), n.size());
      }
    });
  };
  auto renderFloor = [&](anari::Sampler map,
                         float3 normal = float3(0.f),
                         bool mirrorU = false) {
    return render(
        d, makeWorld(d, {floor(material(map), normal, mirrorU)}, {light()}));
  };

  const float s = 0.5f;
  const float c = std::sqrt(1.f - s * s);
  const float tolerance = 1e-4f;
  const Image flat = renderFloor(nullptr);
  const Image tiltedX = renderFloor(nullptr, float3(s, 0.f, c));
  const Image tiltedY = renderFloor(nullptr, float3(0.f, s, c));
  const Image tiltedNegX = renderFloor(nullptr, float3(-s, 0.f, c));
  const Image tiltedNegY = renderFloor(nullptr, float3(0.f, -s, c));
  const float tilt = imageDifference(flat, tiltedX);
  pass &= report("normal map: tilted vertex normals change the shading",
      tilt > 1e-2f && tilt < INFINITY);

  pass &= closeImages("normal map (0, 0, 1) keeps the normal",
      renderFloor(normalMap(d, "image2D", float3(0.f, 0.f, 1.f))),
      flat,
      tolerance);
  pass &= closeImages("normal map tilted along u: as the vertex normal",
      renderFloor(normalMap(d, "image2D", float3(s, 0.f, c))),
      tiltedX,
      tolerance);
  pass &= closeImages("normal map tilted along v: as the vertex normal",
      renderFloor(normalMap(d, "image2D", float3(0.f, s, c))),
      tiltedY,
      tolerance);
  pass &= closeImages("normal map decoded by outTransform/outOffset",
      renderFloor(normalMap(d, "image2D", float3(s, 0.f, c), true)),
      tiltedX,
      tolerance);
  pass &= closeImages("normal map with u mirrored: tangent along -x",
      renderFloor(
          normalMap(d, "image2D", float3(s, 0.f, c)), float3(0.f), true),
      tiltedNegX,
      tolerance);
  pass &=
      closeImages("normal map with inTransform flipping t: bitangent along -y",
          renderFloor(normalMap(d, "image2D", float3(0.f, s, c), false, true)),
          tiltedNegY,
          tolerance);
  pass &= closeImages("transform sampler: not a normal map",
      renderFloor(normalMap(d, "transform", float3(0.f))),
      flat,
      tolerance);

  // a sphere's attributes are constant over it: no normal map
  auto sphere = [&](anari::Sampler map) {
    auto geom = anari::newObject<anari::Geometry>(d, "sphere");
    const float3 center(0.f, 0.f, 0.f);
    const float2 uv(0.25f, 0.75f);
    anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
    anari::setParameterArray1D(d, geom, "vertex.attribute0", &uv, 1);
    anari::setParameter(d, geom, "radius", 0.8f);
    anari::commitParameters(d, geom);
    auto surface = anari::newObject<anari::Surface>(d);
    anari::setAndReleaseParameter(d, surface, "geometry", geom);
    anari::setAndReleaseParameter(d, surface, "material", material(map));
    anari::commitParameters(d, surface);
    return render(d, makeWorld(d, {surface}, {light()}));
  };
  pass &= closeImages("sphere: normal map unused",
      sphere(normalMap(d, "image2D", float3(s, 0.f, c))),
      sphere(nullptr),
      tolerance);
  return pass;
}

// Facing, clamping //////////////////////////////////////////////////////////

bool testBackFaceAndClamp(anari::Device d)
{
  bool pass = true;
  const float3 center(0.2f, 0.3f, 1.f);
  auto light = [&]() {
    return makeLight(d, "point", [=](anari::Device d, anari::Object l) {
      anari::setParameter(d, l, "position", center);
      anari::setParameter(d, l, "intensity", 4.f);
    });
  };
  // the floor wound clockwise seen from the camera: its back faces it
  const std::vector<float3> reversed = {FLOOR[0], FLOOR[3], FLOOR[2], FLOOR[1]};
  pass &= matches("a back face is lit as a front face",
      render(d, makeWorld(d, {makeQuad(d, reversed, matte(d))}, {light()})),
      pointLike(center, 4.f * LIGHT_COLOR));

  auto bright =
      makeLight(d, "directional", [](anari::Device d, anari::Object l) {
        anari::setParameter(d, l, "direction", float3(0.f, 0.f, -1.f));
        anari::setParameter(d, l, "irradiance", 20.f);
      });
  const Image img = renderLit(d, bright);
  pass &= matches("colors above 1 aren't clamped", img, [](float3) {
    return litFloor({{E, 20.f * LIGHT_COLOR}});
  });
  pass &= report("  (the bright floor's red is above 1)",
      !img.color.empty() && img.color[0].x > 1.f);
  return pass;
}

// Ambient occlusion /////////////////////////////////////////////////////////

// A ledge 0.2 above the floor over y > 0.2, which occludes the floor next
// to it (the camera looks straight down), and 'lights'; ambient occlusion
// 'occlusion' at radius 0.3, the renderer configured by 'set'
Image renderLedge(anari::Device d,
    bool occlusion,
    const SetRenderer &set,
    std::vector<anari::Light> lights = {})
{
  const std::vector<float3> ledge = {{-1.f, 0.2f, 0.2f},
      {1.f, 0.2f, 0.2f},
      {1.f, 1.f, 0.2f},
      {-1.f, 1.f, 0.2f}};
  return render(d,
      makeWorld(d, {makeFloor(d), makeQuad(d, ledge, matte(d))}, lights),
      [&](anari::Device d, anari::Renderer r) {
        anari::setParameter(d, r, "ambientOcclusion", occlusion);
        anari::setParameter(d, r, "ambientOcclusionRadius", 0.3f);
        set(d, r);
      });
}

bool testAmbientOcclusion(anari::Device d)
{
  bool pass = true;
  const SetRenderer headlightOnly = [](anari::Device d, anari::Renderer r) {
    anari::setParameter(d, r, "eyeLightBlendRatio", 1.f);
  };
  pass &= report("ambient occlusion leaves the headlight alone",
      sameImage(renderLedge(d, true, headlightOnly),
          renderLedge(d, false, headlightOnly)));

  const SetRenderer ambientOnly = [](anari::Device d, anari::Renderer r) {
    anari::setParameter(d, r, "eyeLightBlendRatio", 0.f);
  };
  const Image on = renderLedge(d, true, ambientOnly);
  const Image off = renderLedge(d, false, ambientOnly);
  // the floor next to the ledge (y just below 0.2)
  const size_t i = size_t(IMAGE_SIZE.y / 2 + 2) * IMAGE_SIZE.x + 3;
  const bool darker = on.color.size() == off.color.size() && !on.color.empty()
      && on.color[i].x < off.color[i].x - 1e-3f;
  pass &= report("ambient occlusion darkens the ambient light", darker);

  // a directional light's direct light isn't darkened: with it, the
  // difference ambient occlusion makes is the ambient light's alone
  auto lit = [&](bool occlusion) {
    auto sun =
        makeLight(d, "directional", [](anari::Device d, anari::Object l) {
          anari::setParameter(d, l, "direction", float3(0.f, 0.f, -1.f));
          anari::setParameter(d, l, "irradiance", 2.f);
        });
    return renderLedge(d, occlusion, ambientOnly, {sun});
  };
  const Image litOn = lit(true);
  const Image litOff = lit(false);
  bool sameLoss = !litOn.color.empty() && litOn.color.size() == on.color.size();
  for (size_t p = 0; sameLoss && p < on.color.size(); p++) {
    for (int c = 0; c < 3; c++) {
      const float lossLit = litOff.color[p][c] - litOn.color[p][c];
      const float loss = off.color[p][c] - on.color[p][c];
      sameLoss &= std::fabs(lossLit - loss) < 1e-5f;
    }
  }
  pass &= report("ambient occlusion leaves direct light alone", sameLoss);
  return pass;
}

// Visible lights ////////////////////////////////////////////////////////////

// Whether pixel (x, y) shows the light square [-0.2, 0.2]^2 (skipping
// pixels on its edge)
int lightCoverage(uint32_t x, uint32_t y)
{
  const float3 P = floorPoint(x, y);
  const float half = 1.f / IMAGE_SIZE.x;
  const float m = std::max(std::fabs(P.x), std::fabs(P.y));
  if (m < 0.2f - half)
    return 1;
  if (m > 0.2f + half)
    return 0;
  return -1;
}

bool testVisibleLights(anari::Device d)
{
  bool pass = true;
  const SetObject intensity = [](anari::Device d, anari::Object l) {
    anari::setParameter(d, l, "intensity", 2.f);
  };
  const float3 radiance = LIGHT_COLOR * (2.f / (0.4f * 0.4f));
  const Image hidden = renderLit(d, quadLight(d, "both", intensity));

  // seen from above: both sides emit, so the camera sees the radiance; the
  // depth and primitive ids stay the floor's
  const Image both = renderLit(d, quadLight(d, "both", intensity, true));
  const Expected floorLit = pointLike(float3(0.f, 0.f, 1.f),
      2.f * LIGHT_COLOR,
      [](float3 e) { return std::max(-e.z, 0.f); });
  auto lightOr = [&](float3 seen) {
    return [=](float3 P) {
      const uint32_t x = uint32_t((P.x + 1.f) / 2.f * IMAGE_SIZE.x);
      const uint32_t y = uint32_t((P.y + 1.f) / 2.f * IMAGE_SIZE.y);
      const int c = lightCoverage(x, y);
      return c < 0 ? SKIP : c ? seen : floorLit(P);
    };
  };
  pass &= matches("visible quad: its radiance where the camera sees it",
      both,
      lightOr(radiance));
  pass &= report("visible quad: depth and ids are the floor's",
      !both.depth.empty() && both.depth == hidden.depth
          && both.primId == hidden.primId);

  pass &= matches("visible quad: its back, which doesn't emit, is black",
      renderLit(d, quadLight(d, "front", intensity, true)),
      lightOr(float3(0.f)));

  pass &= report("visible quad behind the floor is hidden",
      sameImage(renderLit(d, quadLight(d, "back", intensity, true, -1.f)),
          renderLit(d, quadLight(d, "back", intensity, false, -1.f))));

  // a sphere and a ring show their radiance too
  auto sphere = makeLight(d, "point", [](anari::Device d, anari::Object l) {
    anari::setParameter(d, l, "position", float3(0.f, 0.f, 1.f));
    anari::setParameter(d, l, "radius", 0.2f);
    anari::setParameter(d, l, "intensity", 2.f);
    anari::setParameter(d, l, "visible", true);
  });
  const Image sphereImg = renderLit(d, sphere);
  const float3 sphereRadiance = LIGHT_COLOR * (2.f / (PI * 0.2f * 0.2f));
  const size_t c = size_t(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2;
  pass &= report("visible sphere: its radiance at its center",
      !sphereImg.color.empty()
          && linalg::length(float3(sphereImg.color[c].x,
                                sphereImg.color[c].y,
                                sphereImg.color[c].z)
                 - sphereRadiance)
              < 1e-4f * linalg::length(sphereRadiance));
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

  bool pass = testRendererLight(d);
  pass &= testDirectional(d);
  pass &= testPointAndSpot(d);
  pass &= testQuadAndRing(d);
  pass &= testHdri(d);
  pass &= testMaterials(d);
  pass &= testNormalMaps(d);
  pass &= testBackFaceAndClamp(d);
  pass &= testAmbientOcclusion(d);
  pass &= testVisibleLights(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors", g_errors == 0);
  pass &= report("no warnings", g_warnings == 0);
  return pass ? 0 : 1;
}
