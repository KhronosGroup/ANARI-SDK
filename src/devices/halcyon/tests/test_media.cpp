// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'quality''s homogeneous interior media (Typhoon's medium transport;
// renderer/pathtracer/Medium.isph), against analytic values:
// - Beer-Lambert: an emissive plate seen through an absorbing medium behind
//   volume-only boundaries ('materialx' volume materials) renders to its
//   emission times exp(-absorption * thickness), exactly (absorbing-only media
//   draw no random numbers), and through a smooth glass block of OpenPBR
//   transmission depth to its share through the clear block times the
//   medium's transmittance, with the interreflections' series;
// - one medium at a time (Typhoon's single owner): inside an absorbing box,
//   a box of another medium nested in it neither enters its medium nor leaves
//   the outer one;
// - shadow rays: a floor lit through an absorbing volume-only slab, and
//   through the glass block, whose straight-through shadow takes the
//   medium's transmittance in place of the surface tint (exactly the clear
//   block's times it);
// - single scattering: a slab of scattering medium lit from behind by a
//   directional light, seen at angles, renders to the analytic single
//   scattering of its Henyey-Greenstein phase function (anisotropy 0.6 and
//   -0.5 and isotropic), so the anisotropy's direction and strength show;
// - instances: a smaller transform of a volume-only box's instance array
//   inside the other is another object (one medium at a time), and a box
//   scaled by its instance takes world-space distances;
// - chromatic furnace: a slab of white medium of different extinctions per
//   channel (alone and with a white grid volume) scatters 1 - exp(-extinction)
//   of the ambient radiance per channel, which needs correct channel-MIS and
//   grid-collision weights;
// - furnace: a sphere of white scattering medium (isotropic and anisotropic)
//   under the ambient light and a background of the same radiance renders to
//   it, also overlapping a transferFunction1D volume of white colour (their
//   extinctions add);
// - a grid volume of black colour inside an absorbing medium: the
//   transmittance is the product of both;
// - the albedo and normal channels at a medium collision: its
//   single-scattering albedo and 0;
// - differences from Typhoon: a camera ray through an absorbing volume-only box
//   that misses shows the background times the transmittance (colour and
//   opacity); the emitter-only segment passes volume-only boundaries (a floor
//   under the ambient light through an absorbing slab renders to its analytic
//   value); and a shadow ray leaving its medium's own surface outwards leaves
//   the medium (light through a slab of absorbing interior depends on the
//   absorption only through the transmittance over its thickness).

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

constexpr uint2 IMAGE_SIZE = {32, 32};
constexpr float PI = 3.14159265358979323846f;

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

float3 expf3(float3 v)
{
  return float3(std::exp(v.x), std::exp(v.y), std::exp(v.z));
}

// Materials //////////////////////////////////////////////////////////////////

// A 'materialx' volume material: a transparent boundary of an interior
// medium of 'absorption', 'scattering' and 'anisotropy' (an anisotropic_vdf)
anari::Material makeVolumeMaterial(
    anari::Device d, float3 absorption, float3 scattering, float anisotropy)
{
  char doc[1024];
  std::snprintf(doc,
      sizeof(doc),
      R"(<?xml version="1.0"?>
<materialx version="1.39">
  <anisotropic_vdf name="vdf" type="VDF">
    <input name="absorption" type="vector3" value="%.9g, %.9g, %.9g" />
    <input name="scattering" type="vector3" value="%.9g, %.9g, %.9g" />
    <input name="anisotropy" type="float" value="%.9g" />
  </anisotropic_vdf>
  <volume name="V" type="volumeshader">
    <input name="vdf" type="VDF" nodename="vdf" />
  </volume>
  <volumematerial name="M" type="material">
    <input name="volumeshader" type="volumeshader" nodename="V" />
  </volumematerial>
</materialx>)",
      absorption.x,
      absorption.y,
      absorption.z,
      scattering.x,
      scattering.y,
      scattering.z,
      anisotropy);
  auto mat = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, mat, "sourceType", "documentInline");
  anari::setParameter(d, mat, "source", doc);
  anari::commitParameters(d, mat);
  return mat;
}

// A smooth thick OpenPBR glass of IOR 1.5, white surface; with a
// 'transmissionDepth' above 0 its 'transmissionColor' is the colour of its
// absorbing interior medium at that depth, otherwise a surface tint
anari::Material makeGlass(
    anari::Device d, float3 transmissionColor, float transmissionDepth)
{
  auto mat = anari::newObject<anari::Material>(d, "openPBR");
  anari::setParameter(d, mat, "base_weight", 0.f);
  anari::setParameter(d, mat, "specular_weight", 1.f);
  anari::setParameter(d, mat, "specular_roughness", 0.f);
  anari::setParameter(d, mat, "specular_ior", 1.5f);
  anari::setParameter(d, mat, "transmission_weight", 1.f);
  anari::setParameter(d, mat, "transmission_color", transmissionColor);
  anari::setParameter(d, mat, "transmission_depth", transmissionDepth);
  anari::setParameter(d, mat, "geometry_thin_walled", false);
  anari::commitParameters(d, mat);
  return mat;
}

// A 'materialx' thick surface of a smooth dielectric of IOR 1.5 (reflection
// and transmission) mixed with a white translucent lobe of weight
// 'translucent' (1: the translucent lobe alone), whose interior medium, with
// 'withMedium', absorbs 'absorption' (an absorption_vdf; MaterialX's
// surfacematerial takes no volume shader, so the document declares a material
// node that takes both): the translucent lobe lets next-event estimation reach
// lights through the surface, also from inside the medium
anari::Material makeTranslucentGlass(anari::Device d,
    float3 absorption,
    float translucent = 0.5f,
    bool withMedium = true)
{
  char volume[1024];
  std::snprintf(volume,
      sizeof(volume),
      R"(
  <absorption_vdf name="vdf" type="VDF">
    <input name="absorption" type="vector3" value="%.9g, %.9g, %.9g" />
  </absorption_vdf>
  <volume name="V" type="volumeshader">
    <input name="vdf" type="VDF" nodename="vdf" />
  </volume>
  <nodedef name="ND_surfacevolumematerial" node="surfacevolumematerial">
    <input name="surfaceshader" type="surfaceshader" value="" />
    <input name="volumeshader" type="volumeshader" value="" />
    <output name="out" type="material" />
  </nodedef>
  <surfacevolumematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
    <input name="volumeshader" type="volumeshader" nodename="V" />
  </surfacevolumematerial>)",
      absorption.x,
      absorption.y,
      absorption.z);
  const char *surfaceOnly = R"(
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>)";
  char doc[4096];
  std::snprintf(doc,
      sizeof(doc),
      R"(<?xml version="1.0"?>
<materialx version="1.39">
  <dielectric_bsdf name="glass" type="BSDF">
    <input name="ior" type="float" value="1.5" />
    <input name="roughness" type="vector2" value="0, 0" />
    <input name="scatter_mode" type="string" value="RT" />
  </dielectric_bsdf>
  <translucent_bsdf name="translucent" type="BSDF">
    <input name="color" type="color3" value="1, 1, 1" />
  </translucent_bsdf>
  <mix name="mix" type="BSDF">
    <input name="fg" type="BSDF" nodename="translucent" />
    <input name="bg" type="BSDF" nodename="glass" />
    <input name="mix" type="float" value="%.9g" />
  </mix>
  <surface name="S" type="surfaceshader">
    <input name="bsdf" type="BSDF" nodename="mix" />
  </surface>%s
</materialx>)",
      translucent,
      withMedium ? volume : surfaceOnly);
  auto mat = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, mat, "sourceType", "documentInline");
  anari::setParameter(d, mat, "source", doc);
  anari::commitParameters(d, mat);
  return mat;
}

// A black surface that emits 'emission' (native physicallyBased)
anari::Material makeEmitter(anari::Device d, float3 emission)
{
  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(0.f));
  anari::setParameter(d, mat, "emissive", emission);
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

// A 'materialx' thick standard_surface of rough transmission (a dielectric
// that isn't coupled, so shadow rays pass it straight through with caustics
// off) of IOR 1.5 whose 'transmissionColor' at 'transmissionDepth' colours
// its absorbing interior medium (absorption -ln(colour) / depth)
anari::Material makeStandardSurfaceGlass(
    anari::Device d, float3 transmissionColor, float transmissionDepth)
{
  char doc[2048];
  std::snprintf(doc,
      sizeof(doc),
      R"(<?xml version="1.0"?>
<materialx version="1.39">
  <standard_surface name="S" type="surfaceshader">
    <input name="base" type="float" value="0" />
    <input name="specular_roughness" type="float" value="0.3" />
    <input name="specular_IOR" type="float" value="1.5" />
    <input name="transmission" type="float" value="1" />
    <input name="transmission_color" type="color3" value="%.9g, %.9g, %.9g" />
    <input name="transmission_depth" type="float" value="%.9g" />
  </standard_surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)",
      transmissionColor.x,
      transmissionColor.y,
      transmissionColor.z,
      transmissionDepth);
  auto mat = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, mat, "sourceType", "documentInline");
  anari::setParameter(d, mat, "source", doc);
  anari::commitParameters(d, mat);
  return mat;
}

// Geometry and worlds ////////////////////////////////////////////////////////

anari::Surface makeSurface(
    anari::Device d, anari::Geometry geom, anari::Material mat)
{
  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setAndReleaseParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  return surface;
}

// The box [lo, hi] of triangles, faces outward
anari::Geometry makeBox(anari::Device d, float3 lo, float3 hi)
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

// The box [-r, r]^3 of triangles, faces inward, black and non-metallic
// (physicallyBased's default metal of colour 0 still reflects at grazing
// angles), emitting 'radiance' inwards
anari::Surface makeEnclosure(anari::Device d, float r, float radiance)
{
  std::vector<float3> v;
  for (int i = 0; i < 8; i++)
    v.push_back(float3(i & 1 ? r : -r, i & 2 ? r : -r, i & 4 ? r : -r));
  const std::vector<uint3> indices = {{0, 3, 2},
      {0, 1, 3},
      {4, 7, 5},
      {4, 6, 7},
      {0, 5, 1},
      {0, 4, 5},
      {2, 7, 6},
      {2, 3, 7},
      {0, 6, 4},
      {0, 2, 6},
      {1, 7, 3},
      {1, 5, 7}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  auto mat = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, mat, "baseColor", float3(0.f));
  anari::setParameter(d, mat, "metallic", 0.f);
  anari::setParameter(d, mat, "emissive", float3(radiance));
  anari::commitParameters(d, mat);
  return makeSurface(d, geom, mat);
}

// The plane z = 'z' over [-s, s]^2, facing +z
anari::Geometry makePlate(anari::Device d, float s, float z)
{
  const std::vector<float3> v = {
      {-s, -s, z}, {s, -s, z}, {s, s, z}, {-s, s, z}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  anari::commitParameters(d, geom);
  return geom;
}

anari::Geometry makeSphere(anari::Device d, float3 center, float r)
{
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &center, 1);
  anari::setParameter(d, geom, "radius", r);
  anari::commitParameters(d, geom);
  return geom;
}

// A transferFunction1D volume of constant 'color' and 'opacity' over the box
// [lower, upper] (extinction -ln(1 - opacity) per unit length)
anari::Volume makeGridVolume(
    anari::Device d, float3 lower, float3 upper, float3 color, float opacity)
{
  const std::vector<float> voxels(8, 1.f);
  auto field = anari::newObject<anari::SpatialField>(d, "structuredRegular");
  anari::setParameterArray3D(d, field, "data", voxels.data(), 2, 2, 2);
  anari::setParameter(d, field, "origin", lower);
  anari::setParameter(d, field, "spacing", upper - lower);
  anari::commitParameters(d, field);
  auto vol = anari::newObject<anari::Volume>(d, "transferFunction1D");
  anari::setAndReleaseParameter(d, vol, "value", field);
  anari::setParameter(d, vol, "color", color);
  anari::setParameter(d, vol, "opacity", opacity);
  anari::setParameter(d, vol, "unitDistance", 1.f);
  anari::commitParameters(d, vol);
  return vol;
}

anari::Light makeDirectional(anari::Device d, float3 direction, float E)
{
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", direction);
  anari::setParameter(d, light, "irradiance", E);
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
  if (!lights.empty())
    anari::setParameterArray1D(d, world, "light", lights.data(), lights.size());
  if (!volumes.empty()) {
    anari::setParameterArray1D(
        d, world, "volume", volumes.data(), volumes.size());
  }
  anari::commitParameters(d, world);
  for (auto s : surfaces)
    anari::release(d, s);
  for (auto l : lights)
    anari::release(d, l);
  for (auto v : volumes)
    anari::release(d, v);
  return world;
}

// Rendering //////////////////////////////////////////////////////////////////

struct Settings
{
  int pixelSamples{16};
  int maxBounces{16};
  // the ambient light's radiance (white) and the background's
  float ambient{0.f};
  float background{0.f};
  // the background colour and alpha, in place of 'background', where its
  // alpha is at least 0
  float4 backgroundColor{0.f, 0.f, 0.f, -1.f};
  int seed{-1};
  // the orthographic camera: position, direction and view height
  float3 position{0.f, 0.f, 6.f};
  float3 direction{0.f, 0.f, -1.f};
  float height{2.f};
  bool albedoNormal{false};
  bool enableCaustics{false};
};

struct Image
{
  std::vector<float4> color;
  std::vector<float3> albedo;
  std::vector<float3> normal;
};

Image render(anari::Device d, anari::World world, const Settings &s)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  anari::setParameter(d,
      renderer,
      "background",
      s.backgroundColor.w >= 0.f
          ? s.backgroundColor
          : float4(s.background, s.background, s.background, 1.f));
  anari::setParameter(d, renderer, "ambientColor", float3(1.f));
  anari::setParameter(d, renderer, "ambientRadiance", s.ambient);
  anari::setParameter(d, renderer, "pixelSamples", s.pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", s.maxBounces);
  anari::setParameter(d, renderer, "randomNumberSeed", s.seed);
  anari::setParameter(d, renderer, "fireflyClampThreshold", 0.f);
  anari::setParameter(d, renderer, "enableCaustics", s.enableCaustics);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", s.position);
  anari::setParameter(d, camera, "direction", s.direction);
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", s.height);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  if (s.albedoNormal) {
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
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    image.color.assign(fb.data, fb.data + fb.width * fb.height);
  anari::unmap(d, frame, "channel.color");
  if (s.albedoNormal) {
    auto a = anari::map<float3>(d, frame, "channel.albedo");
    if (a.data)
      image.albedo.assign(a.data, a.data + a.width * a.height);
    anari::unmap(d, frame, "channel.albedo");
    auto n = anari::map<float3>(d, frame, "channel.normal");
    if (n.data)
      image.normal.assign(n.data, n.data + n.width * n.height);
    anari::unmap(d, frame, "channel.normal");
  }
  anari::release(d, frame);
  return image;
}

// The mean colour of the pixels within 'fraction' of the image's half size
// around its center (a square)
float3 centerMean(const std::vector<float4> &image, float fraction)
{
  double sum[3] = {};
  size_t n = 0;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float cx = (x + 0.5f) / IMAGE_SIZE.x - 0.5f;
      const float cy = (y + 0.5f) / IMAGE_SIZE.y - 0.5f;
      if (std::abs(cx) >= 0.5f * fraction || std::abs(cy) >= 0.5f * fraction)
        continue;
      const float4 p = image[size_t(y) * IMAGE_SIZE.x + x];
      for (int c = 0; c < 3; c++)
        sum[c] += p[c];
      n++;
    }
  }
  return n ? float3(float(sum[0]), float(sum[1]), float(sum[2])) / float(n)
           : float3(0.f);
}

// Whether every component of 'value' is within 'tolerance' (relative) of
// 'expected', printing both
bool close(const char *what, float3 value, float3 expected, float tolerance)
{
  bool ok = true;
  for (int c = 0; c < 3; c++) {
    ok = ok
        && std::abs(value[c] - expected[c])
            <= tolerance * std::max(std::abs(expected[c]), 1e-6f);
  }
  std::printf("  %s: %s, expected %s\n",
      what,
      str(value).c_str(),
      str(expected).c_str());
  return ok;
}

// Whether every pixel in the center square is within 'tolerance' of
// 'expected'
bool allPixelsClose(const std::vector<float4> &image,
    float fraction,
    float3 expected,
    float tolerance)
{
  if (image.empty())
    return false;
  for (uint32_t y = 0; y < IMAGE_SIZE.y; y++) {
    for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
      const float cx = (x + 0.5f) / IMAGE_SIZE.x - 0.5f;
      const float cy = (y + 0.5f) / IMAGE_SIZE.y - 0.5f;
      if (std::abs(cx) >= 0.5f * fraction || std::abs(cy) >= 0.5f * fraction)
        continue;
      const float4 p = image[size_t(y) * IMAGE_SIZE.x + x];
      for (int c = 0; c < 3; c++) {
        if (std::abs(p[c] - expected[c])
            > tolerance * std::max(expected[c], 1e-6f)) {
          std::printf("  pixel (%u, %u) is %s, expected %s\n",
              x,
              y,
              str(float3(p.x, p.y, p.z)).c_str(),
              str(expected).c_str());
          return false;
        }
      }
    }
  }
  return true;
}

// Tests //////////////////////////////////////////////////////////////////////

const float3 EMISSION = {1.f, 2.f, 0.5f};

// An emissive plate at z = 0, seen from above through 'surfaces'
anari::World plateWorld(anari::Device d,
    std::vector<anari::Surface> surfaces,
    std::vector<anari::Volume> volumes = {})
{
  surfaces.push_back(
      makeSurface(d, makePlate(d, 10.f, 0.f), makeEmitter(d, EMISSION)));
  return makeWorld(d, surfaces, {}, volumes);
}

bool testBeerLambert(anari::Device d)
{
  bool ok = true;
  // a volume-only box of absorption 'a' over z in [1, 3]
  const float3 a(0.5f, 1.f, 2.f);
  auto world = plateWorld(d,
      {makeSurface(d,
          makeBox(d, float3(-2.f, -2.f, 1.f), float3(2.f, 2.f, 3.f)),
          makeVolumeMaterial(d, a, float3(0.f), 0.f))});
  const Image image = render(d, world, Settings{});
  anari::release(d, world);
  const float3 expected = EMISSION * expf3(-2.f * a);
  ok &= report(
      "an absorbing medium between volume-only boundaries: "
      "emission times exp(-absorption * thickness), every pixel",
      allPixelsClose(image.color, 0.9f, expected, 1e-5f));

  // a smooth glass block over z in [1, 3] of transmission colour 'c' at
  // depth 1, against the clear block (white, no depth): the light that
  // passes straight through (1 - F)^2 T, plus the interreflections'
  // (1 - F)^2 T (F T)^2 + ..., over the clear block's (T = 1)
  const float3 c(0.8f, 0.5f, 0.2f);
  const float F = 0.04f; // ((1.5 - 1) / (1.5 + 1))^2, normal incidence
  Settings s;
  s.pixelSamples = 64;
  float3 means[2];
  for (int withMedium = 0; withMedium < 2; withMedium++) {
    auto glass = plateWorld(d,
        {makeSurface(d,
            makeBox(d, float3(-2.f, -2.f, 1.f), float3(2.f, 2.f, 3.f)),
            withMedium ? makeGlass(d, c, 1.f)
                       : makeGlass(d, float3(1.f), 0.f))});
    means[withMedium] = centerMean(render(d, glass, s).color, 0.9f);
    anari::release(d, glass);
  }
  float3 expectedRatio;
  for (int k = 0; k < 3; k++) {
    const float T = std::pow(c[k], 2.f);
    expectedRatio[k] = T * (1.f - F * F) / (1.f - F * F * T * T);
  }
  const float clearExpected = (1.f - F) * (1.f - F) / (1.f - F * F);
  ok &= report(
      "through a clear smooth glass block: the emission's (1 - F)^2 / "
      "(1 - F^2)",
      close("clear", means[0] / EMISSION, float3(clearExpected), 0.01f));
  ok &= report(
      "through a smooth glass block of transmission depth: the clear "
      "block's times its medium's transmittance (with interreflections)",
      close("ratio", means[1] / means[0], expectedRatio, 0.01f));
  return ok;
}

bool testNested(anari::Device d)
{
  // an absorbing box A over z in [1, 4] holding a box B of another medium
  // (z in [2, 3]): Typhoon's single medium makes B's interior A's, so the
  // light through both takes exp(-a_A * 3) alone
  const float3 aA(0.3f, 0.6f, 0.9f);
  const float3 aB(4.f, 4.f, 4.f);
  auto world = plateWorld(d,
      {makeSurface(d,
           makeBox(d, float3(-3.f, -3.f, 1.f), float3(3.f, 3.f, 4.f)),
           makeVolumeMaterial(d, aA, float3(0.f), 0.f)),
          makeSurface(d,
              makeBox(d, float3(-2.f, -2.f, 2.f), float3(2.f, 2.f, 3.f)),
              makeVolumeMaterial(d, aB, float3(0.f), 0.f))});
  const Image image = render(d, world, Settings{});
  anari::release(d, world);
  return report(
      "nested media: one at a time, the outer box's medium through the "
      "inner box",
      allPixelsClose(image.color, 0.9f, EMISSION * expf3(-3.f * aA), 1e-5f));
}

anari::Instance makeInstance(
    anari::Device d, anari::Surface surface, const std::vector<mat4> &xfms)
{
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::commitParameters(d, group);
  anari::release(d, surface);
  auto inst = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, inst, "group", group);
  anari::setParameterArray1D(d, inst, "transform", xfms.data(), xfms.size());
  anari::commitParameters(d, inst);
  return inst;
}

mat4 scaleTranslate(float3 scale, float3 t)
{
  mat4 m = identity;
  m[0][0] = scale.x;
  m[1][1] = scale.y;
  m[2][2] = scale.z;
  m[3] = float4(t, 1.f);
  return m;
}

bool testInstances(anari::Device d)
{
  bool ok = true;
  const float3 a(0.3f, 0.6f, 0.9f);
  const auto plateWithInstance = [&](anari::Instance inst) {
    auto world = anari::newObject<anari::World>(d);
    auto plate =
        makeSurface(d, makePlate(d, 10.f, 0.f), makeEmitter(d, EMISSION));
    anari::setParameterArray1D(d, world, "surface", &plate, 1);
    anari::setParameterArray1D(d, world, "instance", &inst, 1);
    anari::commitParameters(d, world);
    anari::release(d, plate);
    anari::release(d, inst);
    return world;
  };

  // an instance array of a volume-only box over z in [1, 3], the second
  // transform half its size inside the first: the owner is the instance,
  // its transform and the surface, so the inner copy's surfaces neither
  // leave the outer medium nor enter theirs (exp(-a * 2) throughout)
  auto world = plateWithInstance(makeInstance(d,
      makeSurface(d,
          makeBox(d, float3(-2.f, -2.f, 1.f), float3(2.f, 2.f, 3.f)),
          makeVolumeMaterial(d, a, float3(0.f), 0.f)),
      {scaleTranslate(float3(1.f), float3(0.f)),
          scaleTranslate(float3(0.5f), float3(0.f, 0.f, 1.f))}));
  Image image = render(d, world, Settings{});
  anari::release(d, world);
  ok &= report(
      "an instance array: a transform nested in another is another object, "
      "the outer medium throughout",
      allPixelsClose(image.color, 0.9f, EMISSION * expf3(-2.f * a), 1e-5f));

  // a box of thickness 1 scaled by 2 by its instance: distances are world
  // space, so exp(-a * 2)
  world = plateWithInstance(makeInstance(d,
      makeSurface(d,
          makeBox(d, float3(-1.f, -1.f, 0.f), float3(1.f, 1.f, 1.f)),
          makeVolumeMaterial(d, a, float3(0.f), 0.f)),
      {scaleTranslate(float3(2.f), float3(0.f, 0.f, 1.f))}));
  image = render(d, world, Settings{});
  anari::release(d, world);
  ok &= report("a scaled instance: the medium's distances are the world's",
      allPixelsClose(image.color, 0.9f, EMISSION * expf3(-2.f * a), 1e-5f));
  return ok;
}

bool testShadows(anari::Device d)
{
  bool ok = true;
  // a matte floor at z = 0 under a directional light straight down, seen
  // from below the blockers (z = 0.5); direct light only
  const float albedo = 0.8f;
  const float E = 2.f;
  Settings s;
  s.position = float3(0.f, 0.f, 0.5f);
  s.maxBounces = 0;
  s.pixelSamples = 4;
  const auto floorWorld = [&](anari::Surface blocker) {
    return makeWorld(d,
        {makeSurface(d, makePlate(d, 10.f, 0.f), makeMatte(d, float3(albedo))),
            blocker},
        {makeDirectional(d, float3(0.f, 0.f, -1.f), E)});
  };

  // a volume-only slab of absorption 'a', z in [1, 2.5]
  const float3 a(0.2f, 0.7f, 1.5f);
  auto world = floorWorld(makeSurface(d,
      makeBox(d, float3(-5.f, -5.f, 1.f), float3(5.f, 5.f, 2.5f)),
      makeVolumeMaterial(d, a, float3(0.f), 0.f)));
  const Image slab = render(d, world, s);
  anari::release(d, world);
  ok &= report(
      "shadow rays through an absorbing medium: albedo / pi E exp(-a h)",
      allPixelsClose(
          slab.color, 0.9f, float3(albedo / PI * E) * expf3(-1.5f * a), 1e-5f));

  // glass blocks over z in [1, 2]: of transmission colour 'c' at depth 0.5,
  // and clear
  const float3 c(0.9f, 0.6f, 0.3f);
  float3 means[2];
  for (int withMedium = 0; withMedium < 2; withMedium++) {
    auto glass = floorWorld(makeSurface(d,
        makeBox(d, float3(-5.f, -5.f, 1.f), float3(5.f, 5.f, 2.f)),
        withMedium ? makeGlass(d, c, 0.5f) : makeGlass(d, float3(1.f), 0.f)));
    means[withMedium] = centerMean(render(d, glass, s).color, 0.9f);
    anari::release(d, glass);
  }
  const float3 T(c.x * c.x, c.y * c.y, c.z * c.z);
  ok &= report(
      "shadow rays through a glass block of transmission depth: the clear "
      "block's straight-through shadow times the medium's transmittance",
      means[0].x > 0.f && close("ratio", means[1] / means[0], T, 1e-5f));
  return ok;
}

// The analytic single scattering of a slab of 'thickness' (scattering and
// extinction coefficients 'sigmaS', 'sigmaT', Henyey-Greenstein 'g') lit by a
// directional light of irradiance 'E' along its normal from behind, seen
// from the front at 'alpha' off the normal
float singleScattering(
    float sigmaS, float sigmaT, float g, float thickness, float E, float alpha)
{
  const float mu = std::cos(alpha);
  // the phase function at the scattering angle alpha (Typhoon's
  // PdfHenyeyGreenstein(wi, wo) with dot(wi, wo) = -cos(alpha))
  const float denominator = 1.f + g * g - 2.f * g * mu;
  const float phase =
      (1.f - g * g) / (4.f * PI * denominator * std::sqrt(denominator));
  // the integral over the camera ray, s from 0 to thickness / mu, of
  // exp(-sigmaT s) exp(-sigmaT (thickness - s mu))
  const float k = sigmaT * (1.f - mu);
  const float length = thickness / mu;
  const float integral = std::exp(-sigmaT * thickness)
      * (k > 0.f ? (1.f - std::exp(-k * length)) / k : length);
  return sigmaS * phase * E * integral;
}

bool testSingleScattering(anari::Device d)
{
  bool ok = true;
  const float sigmaS = 1.f, sigmaA = 0.5f, h = 0.5f, E = 3.f;
  for (float g : {0.6f, -0.5f, 0.f}) {
    auto world = makeWorld(d,
        {makeSurface(d,
            makeBox(d, float3(-50.f, -50.f, 0.f), float3(50.f, 50.f, h)),
            makeVolumeMaterial(d, float3(sigmaA), float3(sigmaS), g))},
        {makeDirectional(d, float3(0.f, 0.f, 1.f), E)});
    for (float degrees : {20.f, 70.f}) {
      const float alpha = degrees * PI / 180.f;
      Settings s;
      s.maxBounces = 0;
      s.pixelSamples = 256;
      s.direction = float3(std::sin(alpha), 0.f, -std::cos(alpha));
      s.position = float3(0.f, 0.f, h + 1.f) - 2.f * s.direction;
      const float3 mean = centerMean(render(d, world, s).color, 1.f);
      const float expected =
          singleScattering(sigmaS, sigmaS + sigmaA, g, h, E, alpha);
      char name[160];
      std::snprintf(name,
          sizeof(name),
          "single scattering of anisotropy %g seen %g degrees off the light: "
          "the analytic value",
          g,
          degrees);
      ok &= report(name, close("radiance", mean, float3(expected), 0.01f));
    }
    anari::release(d, world);
  }
  return ok;
}

bool testFurnace(anari::Device d)
{
  bool ok = true;
  Settings s;
  s.ambient = 1.f;
  s.background = 1.f;
  s.pixelSamples = 64;
  s.maxBounces = 64;
  s.height = 2.2f;
  for (float g : {0.f, 0.7f}) {
    auto world = makeWorld(d,
        {makeSurface(d,
            makeSphere(d, float3(0.f), 1.f),
            makeVolumeMaterial(d, float3(0.f), float3(2.f), g))});
    const float3 mean = centerMean(render(d, world, s).color, 0.6f);
    anari::release(d, world);
    char name[128];
    std::snprintf(name,
        sizeof(name),
        "furnace: a sphere of white medium (anisotropy %g) under the ambient "
        "light renders to its radiance",
        g);
    ok &= report(name, close("radiance", mean, float3(1.f), 0.02f));
  }

  // with a white grid volume over half of it: their extinctions add
  auto world = makeWorld(d,
      {makeSurface(d,
          makeSphere(d, float3(0.f), 1.f),
          makeVolumeMaterial(d, float3(0.f), float3(1.5f), 0.3f))},
      {},
      {makeGridVolume(d,
          float3(-0.2f, -2.f, -2.f),
          float3(2.f, 2.f, 2.f),
          float3(1.f),
          0.8f)});
  const float3 mean = centerMean(render(d, world, s).color, 0.6f);
  anari::release(d, world);
  ok &= report(
      "furnace: overlapping a white transferFunction1D volume, still its "
      "radiance",
      close("radiance", mean, float3(1.f), 0.02f));

  // a slab (z in [0, 1]) of chromatic white medium (scattering (1, 3, 6),
  // no absorption) under the ambient light, seen against a black
  // background: what scatters towards the camera is the furnace's radiance
  // times 1 - exp(-sigma), per channel (camera rays that pass show the
  // background, whatever their weight). The channels' extinctions differ, so
  // collisions and passes are weighed against the channel mixture's pdf, and
  // a grid volume's collisions (white, opacity 0.8 over the same slab:
  // extinction -ln(0.2) more) against the medium's; only correct weights
  // give these values.
  const float3 sigma(1.f, 3.f, 6.f);
  for (int withGrid = 0; withGrid < 2; withGrid++) {
    std::vector<anari::Volume> volumes;
    float3 extinction = sigma;
    if (withGrid) {
      volumes.push_back(makeGridVolume(d,
          float3(-10.f, -10.f, 0.f),
          float3(10.f, 10.f, 1.f),
          float3(1.f),
          0.8f));
      extinction = extinction - float3(std::log(0.2f));
    }
    auto slab = makeWorld(d,
        {makeSurface(d,
            makeBox(d, float3(-10.f, -10.f, 0.f), float3(10.f, 10.f, 1.f)),
            makeVolumeMaterial(d, float3(0.f), sigma, 0.3f))},
        {},
        volumes);
    Settings c = s;
    c.background = 0.f;
    c.pixelSamples = 128;
    c.maxBounces = 1024;
    const float3 m = centerMean(render(d, slab, c).color, 1.f);
    anari::release(d, slab);
    ok &= report(withGrid
            ? "furnace: a slab of chromatic white medium and white "
              "transferFunction1D volume scatters 1 - exp(-extinction) of "
              "the radiance, per channel"
            : "furnace: a slab of chromatic white medium scatters "
              "1 - exp(-extinction) of the radiance, per channel",
        close("radiance", m, float3(1.f) - expf3(-1.f * extinction), 0.02f));
  }
  return ok;
}

bool testGridVolume(anari::Device d)
{
  // an absorbing volume-only box over z in [1, 3] and a black grid volume
  // (it absorbs what collides) of opacity 0.5 over z in [2, 4]: the plate
  // shows through exp(-2 a) (1 - 0.5)^2
  const float3 a(0.25f, 0.5f, 1.f);
  auto world = plateWorld(d,
      {makeSurface(d,
          makeBox(d, float3(-2.f, -2.f, 1.f), float3(2.f, 2.f, 3.f)),
          makeVolumeMaterial(d, a, float3(0.f), 0.f))},
      {makeGridVolume(d,
          float3(-2.f, -2.f, 2.f),
          float3(2.f, 2.f, 4.f),
          float3(0.f),
          0.5f)});
  Settings s;
  s.pixelSamples = 256;
  const float3 mean = centerMean(render(d, world, s).color, 0.9f);
  anari::release(d, world);
  return report(
      "a grid volume in an absorbing medium: the product of their "
      "transmittances",
      close("radiance", mean, EMISSION * expf3(-2.f * a) * 0.25f, 0.01f));
}

bool testAlbedoNormal(anari::Device d)
{
  // a dense medium of albedo 0.25 / (0.25 + 0.75) in red, 1 in green and
  // blue: almost every camera ray collides at once
  auto world = makeWorld(d,
      {makeSurface(d,
          makeBox(d, float3(-2.f, -2.f, -2.f), float3(2.f, 2.f, 2.f)),
          makeVolumeMaterial(
              d, float3(150.f, 0.f, 0.f), float3(50.f, 200.f, 200.f), 0.f))});
  Settings s;
  s.albedoNormal = true;
  s.maxBounces = 2;
  const Image image = render(d, world, s);
  anari::release(d, world);
  double albedo[3] = {}, normal = 0.;
  for (size_t i = 0; i < image.albedo.size(); i++) {
    for (int c = 0; c < 3; c++) {
      albedo[c] += image.albedo[i][c];
      normal += std::abs(image.normal[i][c]);
    }
  }
  const float n = float(image.albedo.size());
  const float3 mean(albedo[0] / n, albedo[1] / n, albedo[2] / n);
  return report(
      "albedo and normal channels at a medium collision: its "
      "single-scattering albedo, normal 0",
      !image.albedo.empty() && normal == 0.
          && close("albedo", mean, float3(0.25f, 1.f, 1.f), 1e-3f));
}

// 2 E3(tau): the fraction of a uniform dome's irradiance (over pi) that
// reaches a horizontal floor through a horizontal absorbing slab of optical
// thickness 'tau', 2 * integral over mu in [0, 1] of mu exp(-tau / mu)
// (Simpson's rule)
double transmittedDomeFraction(double tau)
{
  const int n = 20000;
  const auto f = [&](double mu) {
    return mu > 0. ? mu * std::exp(-tau / mu) : 0.;
  };
  double sum = f(0.) + f(1.);
  for (int i = 1; i < n; i++)
    sum += (i % 2 ? 4. : 2.) * f(double(i) / n);
  return 2. * sum / (3. * n);
}

// Where transport through interior media differs from Typhoon's: camera
// misses, the emitter-only segment, shadow rays leaving a medium
bool testTyphoonDifferences(anari::Device d)
{
  bool ok = true;

  // a camera ray through a volume-only box of absorption 'a' (z in [1, 3])
  // that then misses shows the background weighed by the transmittance
  // exp(-2 a), and the opacity 1 - T (1 - background alpha), T the mean of
  // the channels' transmittances (Typhoon shows the background unweighted)
  {
    const float3 a(0.5f, 1.f, 2.f);
    auto world = makeWorld(d,
        {makeSurface(d,
            makeBox(d, float3(-2.f, -2.f, 1.f), float3(2.f, 2.f, 3.f)),
            makeVolumeMaterial(d, a, float3(0.f), 0.f))});
    Settings s;
    s.pixelSamples = 1;
    s.backgroundColor = float4(0.2f, 0.4f, 0.6f, 0.5f);
    const Image image = render(d, world, s);
    anari::release(d, world);
    const float3 T = expf3(-2.f * a);
    const float3 expected = float3(0.2f, 0.4f, 0.6f) * T;
    const float alpha = 1.f - (T.x + T.y + T.z) / 3.f * 0.5f;
    bool alphaOk = !image.color.empty();
    for (uint32_t y = 0; alphaOk && y < IMAGE_SIZE.y; y++) {
      for (uint32_t x = 0; x < IMAGE_SIZE.x; x++) {
        const float cx = (x + 0.5f) / IMAGE_SIZE.x - 0.5f;
        const float cy = (y + 0.5f) / IMAGE_SIZE.y - 0.5f;
        if (std::abs(cx) >= 0.45f || std::abs(cy) >= 0.45f)
          continue;
        const float w = image.color[size_t(y) * IMAGE_SIZE.x + x].w;
        if (std::abs(w - alpha) > 1e-5f) {
          std::printf(
              "  pixel (%u, %u) has alpha %g, expected %g\n", x, y, w, alpha);
          alphaOk = false;
          break;
        }
      }
    }
    ok &= report(
        "a camera ray through an absorbing volume-only box that misses: the "
        "background times exp(-absorption * thickness), every pixel",
        allPixelsClose(image.color, 0.9f, expected, 1e-5f));
    ok &= report(
        "... and opacity 1 - T (1 - background alpha), T the channels' mean "
        "transmittance",
        alphaOk);
  }

  // the emitter-only segment passes volume-only boundaries: a matte floor
  // (z = 0) under the ambient light and an absorbing volume-only slab (z in
  // [1, 2.5]), seen from below the slab, with maxBounces 0 (so the ray
  // leaving the floor is the emitter-only segment, which must reach the dome
  // through the slab and take its transmittance for MIS's BSDF share):
  // albedo times the dome's radiance times 2 E3(a h) per channel
  {
    const float albedo = 0.8f;
    const float3 a(0.2f, 0.7f, 1.5f);
    const float h = 1.5f;
    auto world = makeWorld(d,
        {makeSurface(d, makePlate(d, 100.f, 0.f), makeMatte(d, float3(albedo))),
            makeSurface(d,
                makeBox(d,
                    float3(-100.f, -100.f, 1.f),
                    float3(100.f, 100.f, 1.f + h)),
                makeVolumeMaterial(d, a, float3(0.f), 0.f))});
    Settings s;
    s.position = float3(0.f, 0.f, 0.5f);
    s.maxBounces = 0;
    s.pixelSamples = 64;
    s.ambient = 1.f;
    const float3 mean = centerMean(render(d, world, s).color, 1.f);
    anari::release(d, world);
    float3 expected;
    for (int c = 0; c < 3; c++)
      expected[c] = albedo * float(transmittedDomeFraction(a[c] * h));
    ok &= report(
        "the emitter-only segment through an absorbing volume-only slab: the "
        "floor under the ambient light at albedo 2 E3(absorption * h)",
        close("radiance", mean, expected, 0.01f));
  }

  // a shadow ray leaving the medium's own surface outwards is out of the
  // medium: a slab (z in [1, 1 + h]) of a translucent lobe alone, of
  // absorbing interior, seen from above, lit only from below by a directional
  // light, maxBounces 1. Light reaches the camera only through next-event
  // estimation at the bottom face, from inside the medium after a translucent
  // sample through the top (it enters the medium), a cosine-distributed
  // direction of cosine mu, so h / mu inside: every sample is the same
  // K exp(-a h / mu) (the top face lets no straight shadow ray through), and
  // the image is K 2 E3(a h), against K at absorption 0, per channel.
  // Typhoon's shadow rays keep the medium to the light, which takes all of it
  // under a directional light; and its translucent samples don't enter the
  // medium, which would leave the image at K.
  {
    const float h = 0.5f;
    const float3 a(0.4f, 1.2f, 2.4f);
    float3 means[2];
    for (int i = 0; i < 2; i++) {
      auto world = makeWorld(d,
          {makeSurface(d,
              makeBox(
                  d, float3(-20.f, -20.f, 1.f), float3(20.f, 20.f, 1.f + h)),
              makeTranslucentGlass(d, i ? a : float3(0.f), 1.f))},
          {makeDirectional(d, normalize(float3(0.3f, 0.f, 1.f)), 2.f)});
      Settings s;
      s.maxBounces = 1;
      s.pixelSamples = 64;
      means[i] = centerMean(render(d, world, s).color, 1.f);
      anari::release(d, world);
    }
    float3 expected;
    for (int c = 0; c < 3; c++)
      expected[c] = means[0][c] * float(transmittedDomeFraction(a[c] * h));
    ok &= report(
        "shadow rays leaving a medium's own surface outwards, after a "
        "translucent sample entered it: a translucent slab lit from below "
        "renders at 2 E3(absorption * h) of the clear slab's",
        means[0].x > 0.f && close("radiance", means[1], expected, 0.01f));
  }
  return ok;
}

// A path leaves its medium on any sample crossing its owner outwards, and
// shadow rays going inwards through a surface they pass straight through start
// in its medium
bool testMediumCrossings(anari::Device d)
{
  bool ok = true;

  // a translucent lobe's sample leaving the medium's owner leaves the
  // medium, as next-event estimation from the same vertex does: a slab (z in
  // [1, 1.5]) of smooth glass mixed with a translucent lobe, of absorbing
  // interior, seen from above, renders under the ambient light (next-event
  // estimation and BSDF sampling, weighed by MIS) as in an enclosure
  // emitting the same radiance inwards (BSDF sampling only, so unbiased
  // whatever the media). Typhoon keeps the medium outside after a
  // translucent sample, and the two disagree (here 0.721 against 0.662).
  // With caustics off too, and without a medium, as a translucent sample after
  // a diffuse-like event goes on, as next-event estimation through the same
  // lobe does (Typhoon's ends the path: +8.9% without a medium), and enters the
  // medium as next-event estimation inwards through the lobe starts in it.
  for (int caustics = 1; caustics >= 0; caustics--) {
    for (int withMedium = 1; withMedium >= 0; withMedium--) {
      constexpr int SEEDS = 8;
      double mean[2] = {}, var[2] = {};
      for (int enclosure = 0; enclosure < 2; enclosure++) {
        std::vector<anari::Surface> surfaces = {makeSurface(d,
            makeBox(d, float3(-20.f, -20.f, 1.f), float3(20.f, 20.f, 1.5f)),
            makeTranslucentGlass(d, float3(1.f), 0.5f, withMedium))};
        if (enclosure)
          surfaces.push_back(makeEnclosure(d, 50.f, 1.f));
        auto world = makeWorld(d, surfaces);
        double m[SEEDS];
        for (int k = 0; k < SEEDS; k++) {
          Settings s;
          s.pixelSamples = 64;
          s.maxBounces = 64;
          s.seed = k + 1;
          s.ambient = enclosure ? 0.f : 1.f;
          s.enableCaustics = caustics;
          const float3 c = centerMean(render(d, world, s).color, 1.f);
          m[k] = (c.x + c.y + c.z) / 3.;
          mean[enclosure] += m[k] / SEEDS;
        }
        for (int k = 0; k < SEEDS; k++)
          var[enclosure] += (m[k] - mean[enclosure]) * (m[k] - mean[enclosure]);
        var[enclosure] /= double(SEEDS - 1) * SEEDS;
        anari::release(d, world);
      }
      const double diff = mean[0] - mean[1];
      const double se = std::sqrt(var[0] + var[1]);
      std::printf("  ambient light %.6f, enclosure %.6f "
                  "(%+.3f%%, %.1f standard errors)\n",
          mean[0],
          mean[1],
          100. * diff / mean[1],
          diff / se);
      const std::string what = std::string("a translucent glass slab")
          + (withMedium ? " of absorbing interior" : "")
          + " under the ambient light as in an emitting enclosure, caustics "
          + (caustics ? "on" : "off")
          + " (within 4 standard errors and 0.3%)";
      ok &= report(withMedium && caustics
              ? "a translucent sample leaving the medium's owner leaves the "
                "medium: "
                  + what
              : what,
          std::abs(diff) <= 4. * se && std::abs(diff) <= 3e-3 * mean[1]);
    }
  }

  // a shadow ray going inwards through a surface it passes straight
  // through (caustics off, a dielectric that isn't coupled: standard_surface
  // transmission) starts in its interior medium: a slab (z in [1, 1 + h])
  // seen from above, lit only from below by a directional light at angle
  // theta off its normal (near the peak of the transmission lobe, which
  // refracts the camera's rays straight down), maxBounces 0, so that every pixel is next-event
  // estimation at the top face (rays leaving it find nothing: the delta
  // light is found by light sampling only). Its shadow rays cross the slab
  // straight, h / cos theta inside the medium of absorption -ln(c) / depth
  // (c the transmission colour, which standard_surface's transmission lobe
  // also takes as its tint, whatever the depth), so the image
  // at depth D1 is that at depth D2 times c^((h / cos theta) (1 / D1 -
  // 1 / D2)), every pixel (no random numbers but the camera's, the same in
  // both). Typhoon's start out of the medium, and the depth doesn't change
  // them.
  {
    const float h = 0.5f;
    const float depths[2] = {2.f, 0.4f};
    const float3 c(0.8f, 0.5f, 0.2f);
    const float3 lightDir = normalize(float3(0.15f, 0.f, 1.f));
    std::vector<float4> images[2];
    for (int i = 0; i < 2; i++) {
      auto world = makeWorld(d,
          {makeSurface(d,
              makeBox(
                  d, float3(-20.f, -20.f, 1.f), float3(20.f, 20.f, 1.f + h)),
              makeStandardSurfaceGlass(d, c, depths[i]))},
          {makeDirectional(d, lightDir, 2.f)});
      Settings s;
      s.maxBounces = 0;
      s.pixelSamples = 4;
      images[i] = render(d, world, s).color;
      anari::release(d, world);
    }
    const float L = h / lightDir.z * (1.f / depths[1] - 1.f / depths[0]);
    const float3 T(std::pow(c.x, L), std::pow(c.y, L), std::pow(c.z, L));
    bool exact = !images[0].empty() && images[0].size() == images[1].size();
    float lit = 0.f;
    for (size_t i = 0; exact && i < images[0].size(); i++) {
      lit = std::max(lit, images[0][i].x);
      for (int ch = 0; ch < 3; ch++) {
        const float expected = images[0][i][ch] * T[ch];
        if (std::abs(images[1][i][ch] - expected)
            > 1e-5f * std::max(images[0][i][ch], 1e-6f)) {
          std::printf("  pixel %zu channel %d: %g, expected %g\n",
              i,
              ch,
              images[1][i][ch],
              expected);
          exact = false;
          break;
        }
      }
    }
    std::printf("  slab of depth %g up to %g, transmittance ratio %s\n",
        depths[0],
        lit,
        str(T).c_str());
    ok &= report(
        "a shadow ray going inwards through a straight-through thick surface "
        "starts in its medium: the light through it takes "
        "c^(h / (depth cos theta)), every pixel",
        exact && lit > 0.f);
  }

  // ... but only from a surface hit from outside: a camera inside a box of
  // the same material (z in [0, 4]) sees its bottom face from inside, with the
  // path in no medium, lit from above through the box by a directional light
  // near the face's mirror direction, maxBounces 0. Its shadow rays go
  // inwards through the interior and out of the top face, without the medium
  // (as the path's continuations from there have none), so the image doesn't
  // depend on the depth, every pixel bitwise.
  {
    const float depths[2] = {2.f, 0.4f};
    std::vector<float4> images[2];
    for (int i = 0; i < 2; i++) {
      auto world = makeWorld(d,
          {makeSurface(d,
              makeBox(d, float3(-5.f, -5.f, 0.f), float3(5.f, 5.f, 4.f)),
              makeStandardSurfaceGlass(
                  d, float3(0.8f, 0.5f, 0.2f), depths[i]))},
          {makeDirectional(d, normalize(float3(0.15f, 0.f, -1.f)), 2.f)});
      Settings s;
      s.maxBounces = 0;
      s.pixelSamples = 4;
      s.position = float3(0.f, 0.f, 2.f);
      images[i] = render(d, world, s).color;
      anari::release(d, world);
    }
    bool same = !images[0].empty() && images[0].size() == images[1].size();
    float lit = 0.f;
    for (size_t i = 0; same && i < images[0].size(); i++) {
      lit = std::max(lit, images[0][i].x);
      same = std::memcmp(&images[0][i], &images[1][i], sizeof(float4)) == 0;
    }
    std::printf("  inside the box up to %g\n", lit);
    ok &= report(
        "... from a surface hit from inside with the path in no medium, they "
        "start in none: the image doesn't depend on the depth, bitwise",
        same && lit > 0.f);
  }
  return ok;
}

} // namespace

int main()
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
  ok &= testBeerLambert(d);
  ok &= testNested(d);
  ok &= testInstances(d);
  ok &= testShadows(d);
  ok &= testSingleScattering(d);
  ok &= testFurnace(d);
  ok &= testGridVolume(d);
  ok &= testAlbedoNormal(d);
  ok &= testTyphoonDifferences(d);
  ok &= testMediumCrossings(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  ok &= report("no errors or warnings", g_errors == 0 && g_warnings == 0);
  return ok ? 0 : 1;
}
