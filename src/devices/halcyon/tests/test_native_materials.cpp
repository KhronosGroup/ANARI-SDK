// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The pathtracer's native materials (renderer/pathtracer/PathMatte.isph,
// PathPbm.isph) against the C++ path they stand in for, rendered with the
// renderer's 'nativeMaterials' true and false on the same sample sequences:
// - a box of matte surfaces (constant, per-primitive attribute and image2D
//   sampler colours, a blend sphere, a mask cut-out, a colour above 1, a
//   FLOAT32 colour only the graph reads) with a physicallyBased sphere that
//   stays C++ (a thin film) in the same gangs. Native matte matches its graph
//   to rounding (test_path_material), so paths take the same decisions almost
//   everywhere: the colour, albedo and normal channels must match per pixel
//   within a tolerance, nearly all of them to rounding.
// - a box of physicallyBased surfaces: ANARI's defaults, dielectrics with and
//   without specular, metallic and roughness from an image2D sampler and
//   from a per-primitive attribute, normal maps, clearcoats (glossy, delta,
//   per texel), a sheen, glass (rough, delta, thick, per texel), delta and
//   sharp metals, a sharp and a smooth half-metal, an emissive and a blend
//   sphere, a mask cut-out, and in the same gangs an iridescent one that
//   stays C++. Native
//   physicallyBased rounds as its graph does (PathPbm.ispc), so the renders
//   must be the same but for rounding in what both read (the samplers and
//   attributes); where the C++ rounds differently
//   (native_rounding.h), the colours must agree well within the noise.
// Each scene is lit by a sphere light, a directional light through the open
// front and the ambient light.
//
// Usage: halcyon_test_native_materials [--bench]
// --bench also times renders of each scene both ways and prints the speedup.

#include "native_rounding.h"
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
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

int g_errors = 0;

// The last way each material says the pathtracer shades it (Material.cpp's
// DEBUG message at finalize()): "matte", "physicallyBased" (natively) or
// "C++" (its graph)
std::mutex g_routesMutex;
std::map<ANARIObject, std::string> g_routes;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject source,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_DEBUG) {
    const char *route = std::strstr(message, "natively (matte)") ? "matte"
        : std::strstr(message, "natively (physicallyBased)") ? "physicallyBased"
        : std::strstr(message, "through its MaterialXCpp graph in C++")
        ? "C++"
        : nullptr;
    if (route) {
      std::lock_guard<std::mutex> lock(g_routesMutex);
      g_routes[source] = route;
    }
  }
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr,
        "[ANARI %s] %s\n",
        severity == ANARI_SEVERITY_WARNING ? "WARNING" : "ERROR",
        message);
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

// Scene //////////////////////////////////////////////////////////////////////

anari::Material matte(anari::Device d, float3 color)
{
  auto m = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, m, "color", color);
  anari::commitParameters(d, m);
  return m;
}

// Adds a surface of 'geometry' and 'material' (both released) to 'surfaces'
void addSurface(anari::Device d,
    std::vector<anari::Surface> &surfaces,
    anari::Geometry geometry,
    anari::Material material)
{
  anari::commitParameters(d, geometry);
  auto s = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, s, "geometry", geometry);
  anari::setAndReleaseParameter(d, s, "material", material);
  anari::commitParameters(d, s);
  surfaces.push_back(s);
}

// A quad of corners 'c' (counter-clockwise seen from its front) as two
// triangles, with texture coordinates in attribute0
anari::Geometry quad(anari::Device d, const float3 c[4])
{
  auto g = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, g, "vertex.position", c, 4);
  const uint3 indices[2] = {{0, 1, 2}, {0, 2, 3}};
  anari::setParameterArray1D(d, g, "primitive.index", indices, 2);
  const float2 uv[4] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
  anari::setParameterArray1D(d, g, "vertex.attribute0", uv, 4);
  return g;
}

anari::Geometry sphere(anari::Device d, float3 center, float radius)
{
  auto g = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, g, "vertex.position", &center, 1);
  anari::setParameter(d, g, "radius", radius);
  return g;
}

// The open box [-1, 1]^3 (no front wall)
const float3 FLOOR[4] = {
    {-1.f, -1.f, 1.f}, {1.f, -1.f, 1.f}, {1.f, -1.f, -1.f}, {-1.f, -1.f, -1.f}};
const float3 CEILING[4] = {
    {-1.f, 1.f, -1.f}, {1.f, 1.f, -1.f}, {1.f, 1.f, 1.f}, {-1.f, 1.f, 1.f}};
const float3 BACK[4] = {
    {-1.f, -1.f, -1.f}, {1.f, -1.f, -1.f}, {1.f, 1.f, -1.f}, {-1.f, 1.f, -1.f}};
const float3 LEFT[4] = {
    {-1.f, -1.f, 1.f}, {-1.f, -1.f, -1.f}, {-1.f, 1.f, -1.f}, {-1.f, 1.f, 1.f}};
const float3 RIGHT[4] = {
    {1.f, -1.f, -1.f}, {1.f, -1.f, 1.f}, {1.f, 1.f, 1.f}, {1.f, 1.f, -1.f}};

// A 4x4 image2D sampler of 'texels', linearly or nearest filtered
anari::Sampler image2D(
    anari::Device d, const std::vector<float4> &texels, const char *filter)
{
  auto s = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setParameterArray2D(d, s, "image", texels.data(), 4, 4);
  anari::setParameter(d, s, "filter", filter);
  anari::commitParameters(d, s);
  return s;
}

// The world of 'surfaces' (released), lit by a sphere light, a directional
// light through the open front
anari::World makeWorld(anari::Device d, std::vector<anari::Surface> &surfaces)
{
  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  for (auto s : surfaces)
    anari::release(d, s);

  anari::Light lights[2];
  lights[0] = anari::newObject<anari::Light>(d, "point");
  anari::setParameter(d, lights[0], "position", float3(0.f, 0.75f, 0.f));
  anari::setParameter(d, lights[0], "intensity", 2.f);
  anari::setParameter(d, lights[0], "radius", 0.15f);
  anari::commitParameters(d, lights[0]);
  lights[1] = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, lights[1], "direction", float3(-0.3f, -0.5f, -1.f));
  anari::setParameter(d, lights[1], "irradiance", 1.5f);
  anari::commitParameters(d, lights[1]);
  anari::setParameterArray1D(d, world, "light", lights, 2);
  anari::release(d, lights[0]);
  anari::release(d, lights[1]);

  anari::commitParameters(d, world);
  return world;
}

// The matte box and what is in it
anari::World makeMatteWorld(anari::Device d)
{
  std::vector<anari::Surface> surfaces;
  const float3 *floor = FLOOR, *ceiling = CEILING, *back = BACK, *left = LEFT,
               *right = RIGHT;

  // constant colours; the ceiling's above 1
  addSurface(d, surfaces, quad(d, floor), matte(d, float3(0.8f)));
  addSurface(d, surfaces, quad(d, ceiling), matte(d, float3(1.2f, 1.1f, 1.f)));
  addSurface(d, surfaces, quad(d, left), matte(d, float3(0.8f, 0.1f, 0.1f)));
  // a FLOAT32 'color', which the specification doesn't allow: the graph
  // reads it (0.4, 1, 1), so native matte leaves it to the graph
  {
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, m, "color", 0.4f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.55f, 0.55f, -0.5f), 0.2f), m);
  }

  // the right wall's colour per primitive
  {
    auto g = quad(d, right);
    const float4 colors[2] = {{0.1f, 0.8f, 0.1f, 1.f}, {0.2f, 0.5f, 0.9f, 1.f}};
    anari::setParameterArray1D(d, g, "primitive.color", colors, 2);
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, m, "color", "color");
    anari::commitParameters(d, m);
    addSurface(d, surfaces, g, m);
  }

  // the back wall's colour from a linearly filtered image2D
  {
    std::vector<float4> texels;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++) {
        texels.push_back(
            float4(0.2f + 0.2f * i, 0.8f - 0.2f * j, (i + j) % 2 ? 0.7f : 0.2f, 1.f));
      }
    }
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setAndReleaseParameter(d, m, "color", image2D(d, texels, "linear"));
    anari::commitParameters(d, m);
    addSurface(d, surfaces, quad(d, back), m);
  }

  // a mask cut-out: a nearest checkerboard's alpha, across the box
  {
    std::vector<float4> texels;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++)
        texels.push_back(float4(0.9f, 0.9f, 0.3f, (i + j) % 2 ? 1.f : 0.f));
    }
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setAndReleaseParameter(d, m, "color", image2D(d, texels, "nearest"));
    anari::setParameter(d, m, "alphaMode", "mask");
    anari::commitParameters(d, m);
    const float3 panel[4] = {{-0.9f, -1.f, 0.2f},
        {-0.1f, -1.f, -0.6f},
        {-0.1f, 0.2f, -0.6f},
        {-0.9f, 0.2f, 0.2f}};
    addSurface(d, surfaces, quad(d, panel), m);
  }

  // a blend sphere
  {
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, m, "color", float3(0.9f, 0.6f, 0.2f));
    anari::setParameter(d, m, "opacity", 0.5f);
    anari::setParameter(d, m, "alphaMode", "blend");
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(-0.3f, -0.55f, 0.4f), 0.45f), m);
  }

  // a C++ material in the same gangs (a thin film keeps physicallyBased
  // there)
  {
    auto m = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setParameter(d, m, "baseColor", float3(0.9f, 0.8f, 0.6f));
    anari::setParameter(d, m, "metallic", 1.f);
    anari::setParameter(d, m, "roughness", 0.3f);
    anari::setParameter(d, m, "iridescence", 1.f);
    anari::setParameter(d, m, "iridescenceThickness", 300.f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.5f, -0.6f, -0.2f), 0.4f), m);
  }

  return makeWorld(d, surfaces);
}

anari::Material physicallyBased(
    anari::Device d, float3 baseColor, float metallic, float roughness)
{
  auto m = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, m, "baseColor", baseColor);
  anari::setParameter(d, m, "metallic", metallic);
  anari::setParameter(d, m, "roughness", roughness);
  return m;
}

// The physicallyBased box and what is in it
anari::World makePhysicallyBasedWorld(anari::Device d)
{
  std::vector<anari::Surface> surfaces;

  // ANARI's defaults but the colour: a rough metal
  {
    auto m = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setParameter(d, m, "baseColor", float3(0.8f, 0.8f, 0.75f));
    anari::commitParameters(d, m);
    addSurface(d, surfaces, quad(d, CEILING), m);
  }
  // a dielectric floor: specular 1 (glTF's default), ior 1.5, a normal map
  // of tiles tilted every way, and a glossy clearcoat (on the unperturbed
  // normal)
  {
    std::vector<float4> normals;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++) {
        const float x = 0.35f * float(i - 1.5f), y = 0.35f * float(j - 1.5f);
        const float z = std::sqrt(std::max(0.f, 1.f - x * x - y * y));
        normals.push_back(float4(x, y, z, 1.f));
      }
    }
    auto m = physicallyBased(d, float3(0.6f, 0.55f, 0.5f), 0.f, 0.35f);
    anari::setParameter(d, m, "specular", 1.f);
    anari::setAndReleaseParameter(
        d, m, "normal", image2D(d, normals, "nearest"));
    anari::setParameter(d, m, "clearcoat", 0.6f);
    anari::setParameter(d, m, "clearcoatRoughness", 0.1f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, quad(d, FLOOR), m);
  }
  // a dielectric of ANARI's specular 0 (diffuse only), its colour an
  // attribute per primitive
  {
    auto g = quad(d, LEFT);
    const float4 colors[2] = {{0.8f, 0.1f, 0.1f, 1.f}, {0.7f, 0.5f, 0.1f, 1.f}};
    anari::setParameterArray1D(d, g, "primitive.color", colors, 2);
    auto m = physicallyBased(d, float3(1.f), 0.f, 0.5f);
    anari::setParameter(d, m, "baseColor", "color");
    anari::commitParameters(d, m);
    addSurface(d, surfaces, g, m);
  }
  // metallic per primitive (attribute1), specular colour and ior
  {
    auto g = quad(d, RIGHT);
    const float metallic[2] = {0.2f, 0.9f};
    anari::setParameterArray1D(d, g, "primitive.attribute1", metallic, 2);
    auto m = physicallyBased(d, float3(0.1f, 0.6f, 0.2f), 0.f, 0.25f);
    anari::setParameter(d, m, "metallic", "attribute1");
    anari::setParameter(d, m, "specular", 0.8f);
    anari::setParameter(d, m, "specularColor", float3(1.f, 0.8f, 0.6f));
    anari::setParameter(d, m, "ior", 1.8f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, g, m);
  }
  // roughness and metallic from linearly filtered image2D samplers, base
  // colour from another, and a delta clearcoat where a nearest one says so
  {
    std::vector<float4> roughness, metallic, color, clearcoat;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++) {
        roughness.push_back(float4(0.05f + 0.3f * i, 0.f, 0.f, 1.f));
        metallic.push_back(float4((i + j) % 2 ? 1.f : 0.f, 0.f, 0.f, 1.f));
        color.push_back(float4(
            0.3f + 0.2f * j, 0.7f - 0.1f * i, (i + j) % 2 ? 0.6f : 0.3f, 1.f));
        clearcoat.push_back(float4(j < 2 ? 1.f : 0.f, 0.f, 0.f, 1.f));
      }
    }
    auto m = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setAndReleaseParameter(
        d, m, "roughness", image2D(d, roughness, "linear"));
    anari::setAndReleaseParameter(
        d, m, "metallic", image2D(d, metallic, "linear"));
    anari::setAndReleaseParameter(
        d, m, "baseColor", image2D(d, color, "linear"));
    anari::setAndReleaseParameter(
        d, m, "clearcoat", image2D(d, clearcoat, "nearest"));
    anari::setParameter(d, m, "specular", 1.f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, quad(d, BACK), m);
  }
  // a mask cut-out: a nearest checkerboard's alpha
  {
    std::vector<float4> texels;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++)
        texels.push_back(float4(0.9f, 0.9f, 0.3f, (i + j) % 2 ? 1.f : 0.f));
    }
    auto m = physicallyBased(d, float3(1.f), 0.f, 0.6f);
    anari::setAndReleaseParameter(
        d, m, "baseColor", image2D(d, texels, "nearest"));
    anari::setParameter(d, m, "alphaMode", "mask");
    anari::commitParameters(d, m);
    const float3 panel[4] = {{-0.9f, -1.f, 0.2f},
        {-0.1f, -1.f, -0.6f},
        {-0.1f, 0.2f, -0.6f},
        {-0.9f, 0.2f, 0.2f}};
    addSurface(d, surfaces, quad(d, panel), m);
  }
  // a normal-mapped rough metal sphere: without texture coordinates the map
  // is sampled at attribute0's default, which tilts the whole sphere's
  // normals one way in its tangent frame
  {
    std::vector<float4> normals;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++)
        normals.push_back((i + j) % 2 ? float4(0.3f, -0.2f, 0.93f, 1.f)
                                      : float4(-0.25f, 0.3f, 0.92f, 1.f));
    }
    auto m = physicallyBased(d, float3(0.7f, 0.75f, 0.8f), 1.f, 0.3f);
    anari::setAndReleaseParameter(
        d, m, "normal", image2D(d, normals, "linear"));
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(-0.6f, 0.65f, -0.5f), 0.25f), m);
  }
  // a mirror (delta metal), a sharp half-metal, a blend dielectric that
  // emits
  {
    auto m = physicallyBased(d, float3(0.95f, 0.93f, 0.88f), 1.f, 0.f);
    anari::setParameter(d, m, "clearcoat", 1.f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.5f, -0.6f, -0.2f), 0.4f), m);
  }
  {
    auto m = physicallyBased(d, float3(0.9f, 0.6f, 0.2f), 0.5f, 0.04f);
    anari::setParameter(d, m, "specular", 0.5f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.55f, 0.55f, -0.5f), 0.25f), m);
  }
  // a smooth half-metal: delta lobes in the base's mix
  {
    auto m = physicallyBased(d, float3(0.85f, 0.9f, 0.95f), 0.5f, 0.f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.05f, 0.35f, 0.2f), 0.15f), m);
  }
  {
    auto m = physicallyBased(d, float3(0.2f, 0.4f, 0.9f), 0.f, 0.2f);
    anari::setParameter(d, m, "specular", 1.f);
    anari::setParameter(d, m, "opacity", 0.6f);
    anari::setParameter(d, m, "alphaMode", "blend");
    anari::setParameter(d, m, "emissive", float3(0.3f, 0.2f, 0.1f));
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(-0.3f, -0.55f, 0.4f), 0.35f), m);
  }
  // a sheen
  {
    auto m = physicallyBased(d, float3(0.5f, 0.1f, 0.3f), 0.f, 0.7f);
    anari::setParameter(d, m, "sheenColor", float3(0.8f, 0.8f, 0.9f));
    anari::setParameter(d, m, "sheenRoughness", 0.4f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.f, 0.f, -0.6f), 0.2f), m);
  }
  // glass: rough and thin-walled, and delta and thick with a per-texel
  // transmission on a panel
  {
    auto m = physicallyBased(d, float3(0.9f, 1.f, 0.95f), 0.f, 0.1f);
    anari::setParameter(d, m, "transmission", 1.f);
    anari::setParameter(d, m, "specular", 1.f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(-0.55f, 0.4f, 0.3f), 0.25f), m);
  }
  {
    std::vector<float4> transmission;
    for (int j = 0; j < 4; j++) {
      for (int i = 0; i < 4; i++)
        transmission.push_back(float4(i < 2 ? 1.f : 0.3f, 0.f, 0.f, 1.f));
    }
    auto m = physicallyBased(d, float3(1.f, 0.9f, 0.8f), 0.f, 0.f);
    anari::setParameter(d, m, "specular", 1.f);
    anari::setParameter(d, m, "ior", 1.4f);
    anari::setParameter(d, m, "thickness", 0.1f);
    anari::setAndReleaseParameter(
        d, m, "transmission", image2D(d, transmission, "nearest"));
    anari::commitParameters(d, m);
    const float3 panel[4] = {{0.15f, -1.f, 0.6f},
        {0.85f, -1.f, 0.3f},
        {0.85f, -0.3f, 0.3f},
        {0.15f, -0.3f, 0.6f}};
    addSurface(d, surfaces, quad(d, panel), m);
  }
  // a C++ material in the same gangs: a thin film
  {
    auto m = physicallyBased(d, float3(0.6f, 0.6f, 0.6f), 1.f, 0.3f);
    anari::setParameter(d, m, "iridescence", 1.f);
    anari::setParameter(d, m, "iridescenceThickness", 400.f);
    anari::commitParameters(d, m);
    addSurface(d, surfaces, sphere(d, float3(0.6f, 0.15f, 0.1f), 0.2f), m);
  }

  return makeWorld(d, surfaces);
}

// Rendering //////////////////////////////////////////////////////////////////

struct Image
{
  std::vector<float4> color;
  std::vector<float3> albedo;
  std::vector<float3> normal;
};

struct RenderSettings
{
  uint2 size{64, 64};
  int pixelSamples{16};
  int maxBounces{6};
  bool albedoNormal{true};
  // Each render adds samples (KHR_FRAME_ACCUMULATION)
  bool accumulation{false};
  // The sample generator's seed (the renderer's default, -1, is 0)
  int seed{-1};
};

anari::Frame makeFrame(anari::Device d,
    anari::World world,
    bool nativeMaterials,
    const RenderSettings &settings)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "quality");
  anari::setParameter(d, renderer, "pixelSamples", settings.pixelSamples);
  anari::setParameter(d, renderer, "maxBounces", settings.maxBounces);
  anari::setParameter(d, renderer, "ambientRadiance", 0.3f);
  anari::setParameter(d, renderer, "nativeMaterials", nativeMaterials);
  anari::setParameter(d, renderer, "randomNumberSeed", settings.seed);
  anari::commitParameters(d, renderer);

  auto camera = anari::newObject<anari::Camera>(d, "perspective");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.4f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "fovy", 0.7f);
  anari::setParameter(
      d, camera, "aspect", float(settings.size.x) / settings.size.y);
  anari::commitParameters(d, camera);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", settings.size);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, frame, "accumulation", settings.accumulation);
  if (settings.albedoNormal) {
    anari::setParameter(d, frame, "channel.albedo", ANARI_FLOAT32_VEC3);
    anari::setParameter(d, frame, "channel.normal", ANARI_FLOAT32_VEC3);
  }
  anari::setAndReleaseParameter(d, frame, "camera", camera);
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);
  return frame;
}

template <typename T>
std::vector<T> read(anari::Device d, anari::Frame frame, const char *channel)
{
  std::vector<T> pixels;
  auto fb = anari::map<T>(d, frame, channel);
  if (fb.data)
    pixels.assign(fb.data, fb.data + size_t(fb.width) * fb.height);
  anari::unmap(d, frame, channel);
  return pixels;
}

Image render(anari::Device d,
    anari::World world,
    bool nativeMaterials,
    const RenderSettings &settings = {})
{
  auto frame = makeFrame(d, world, nativeMaterials, settings);
  anari::render(d, frame);
  anari::wait(d, frame);
  Image image;
  image.color = read<float4>(d, frame, "channel.color");
  image.albedo = read<float3>(d, frame, "channel.albedo");
  image.normal = read<float3>(d, frame, "channel.normal");
  anari::release(d, frame);
  return image;
}

// Comparisons ////////////////////////////////////////////////////////////////

// Per-pixel absolute differences of one channel (largest over components)
struct Difference
{
  float max{0.f};
  double mean{0.};
  // Pixels that differ by more than rounding (ROUNDING)
  int beyondRounding{0};
  int pixels{0};
};

constexpr float ROUNDING = 1e-4f;

template <typename T, int N>
Difference difference(const std::vector<T> &a, const std::vector<T> &b)
{
  Difference r;
  r.pixels = int(std::min(a.size(), b.size()));
  for (int i = 0; i < r.pixels; i++) {
    float m = 0.f;
    for (int c = 0; c < N; c++)
      m = std::max(m, std::abs(a[i][c] - b[i][c]));
    r.max = std::max(r.max, m);
    r.mean += m;
    r.beyondRounding += m > ROUNDING;
  }
  r.mean /= std::max(r.pixels, 1);
  return r;
}

std::string describe(const Difference &d)
{
  char s[160];
  std::snprintf(s,
      sizeof(s),
      "max %.3g, mean %.3g, %d of %d pixels beyond %.0e",
      d.max,
      d.mean,
      d.beyondRounding,
      d.pixels,
      double(ROUNDING));
  return s;
}

// Paths that diverge (a direction a few ulps apart on either side of an
// edge) change one sample of a pixel: allow a few such pixels, a bounded
// difference in them, and a small mean
bool close(const Difference &d, int pixels, float maxDifference)
{
  return d.pixels == pixels && d.mean < 2e-4
      && d.beyondRounding <= std::max(pixels / 50, 1) && d.max < maxDifference;
}

using native_rounding::BITWISE;

// Where the C++ may be contracted to FMAs or built with fast math
// (native_rounding.h), native physicallyBased no longer rounds as its
// graph: its near-specular lobes differ by up to tens of percent at single
// points (test_path_pbm), so many paths' values differ and some diverge.
// Both renders are then estimates of the same image on the same samples, and
// their difference must stay well below the noise: its mean below
// NOISE_FRACTION of the mean difference between the graphs' render and one
// with another seed (0.0076 of it with -march=native; a sphere covering a
// tenth of the image 10% darker would give about 0.05). A mean can hide a
// regression of a few pixels, so the pixels whose difference passes their own
// seed-to-seed difference (plus 1e-3) must also be fewer than
// BEYOND_NOISE_FRACTION of the image (4 of 4096 with -march=native: paths
// that diverge at pixels whose other seed happened to agree). The albedo and
// normal channels, from the first non-specular hit, still match per pixel as
// matte's do (close()).
constexpr double NOISE_FRACTION = 0.025;
constexpr double BEYOND_NOISE_FRACTION = 0.01;

// The pixels where 'a' and 'b' differ (largest channel of rgb) by more than
// 'noiseA' and 'noiseB' do, plus 1e-3
int beyondNoise(const std::vector<float4> &a,
    const std::vector<float4> &b,
    const std::vector<float4> &noiseA,
    const std::vector<float4> &noiseB)
{
  const size_t n = std::min(
      std::min(a.size(), b.size()), std::min(noiseA.size(), noiseB.size()));
  int beyond = 0;
  for (size_t i = 0; i < n; i++) {
    float d = 0.f, noise = 0.f;
    for (int c = 0; c < 3; c++) {
      d = std::max(d, std::abs(a[i][c] - b[i][c]));
      noise = std::max(noise, std::abs(noiseA[i][c] - noiseB[i][c]));
    }
    beyond += d > noise + 1e-3f;
  }
  return beyond;
}

// The scene of 'world' rendered both ways. 'roundingOnly': the native kinds
// round as their graphs (physicallyBased, where BITWISE), so only what both
// read (samplers, attributes) may differ, in the last bits;
// otherwise (matte) the native kind rounds differently, so it must show (the
// colours differ somewhere) and a few paths may diverge.
bool testImages(anari::Device d,
    const std::string &scene,
    anari::World world,
    bool roundingOnly)
{
  const RenderSettings settings;
  const int pixels = int(settings.size.x * settings.size.y);
  const Image native = render(d, world, true, settings);
  const Image graphs = render(d, world, false, settings);
  // the noise, where native physicallyBased rounds differently
  const bool withinNoise = roundingOnly && !BITWISE;
  Image reseeded;
  if (withinNoise) {
    RenderSettings other = settings;
    other.seed = 1;
    reseeded = render(d, world, false, other);
  }
  anari::release(d, world);

  bool pass = report(scene + ": renders",
      int(native.color.size()) == pixels && int(graphs.color.size()) == pixels
          && int(native.albedo.size()) == pixels
          && int(native.normal.size()) == pixels);

  double mean = 0.;
  for (const float4 &c : native.color)
    mean += (c.x + c.y + c.z) / 3.f;
  mean /= std::max<size_t>(native.color.size(), 1);
  const bool identical = native.color.size() == graphs.color.size()
      && std::memcmp(native.color.data(),
             graphs.color.data(),
             native.color.size() * sizeof(float4))
          == 0;
  if (roundingOnly) {
    pass &= report(scene + ": the scene is lit",
        mean > 0.05,
        "mean " + std::to_string(mean)
            + (identical ? ", identical renders" : ""));
  } else {
    // the native kind actually ran: the colours differ in the last bits
    pass &= report(scene + ": the scene is lit, and native shading ran",
        mean > 0.05 && !identical,
        "mean " + std::to_string(mean));
  }

  const auto matches = [&](const Difference &diff, float maxDifference) {
    return roundingOnly && BITWISE
        ? diff.pixels == pixels && diff.beyondRounding == 0
        : close(diff, pixels, maxDifference);
  };
  const Difference color = difference<float4, 4>(native.color, graphs.color);
  if (withinNoise) {
    const Difference noise =
        difference<float4, 4>(graphs.color, reseeded.color);
    const int beyond =
        beyondNoise(native.color, graphs.color, graphs.color, reseeded.color);
    pass &= report(scene + ": colour matches the graphs' within the noise",
        color.pixels == pixels && noise.pixels == pixels
            && color.mean < NOISE_FRACTION * noise.mean
            && beyond < BEYOND_NOISE_FRACTION * pixels,
        describe(color) + "; another seed: " + describe(noise) + "; "
            + std::to_string(beyond) + " pixels beyond their own noise");
  } else {
    pass &= report(scene + ": colour matches the graphs' per pixel",
        matches(color, 0.25f),
        describe(color));
  }
  const Difference albedo =
      difference<float3, 3>(native.albedo, graphs.albedo);
  pass &= report(scene + ": albedo matches the graphs' per pixel",
      matches(albedo, 0.1f),
      describe(albedo));
  const Difference normal =
      difference<float3, 3>(native.normal, graphs.normal);
  pass &= report(scene + ": normal matches the graphs' per pixel",
      matches(normal, 0.1f),
      describe(normal));
  return pass;
}

// Routing ////////////////////////////////////////////////////////////////////

// The device routes each material to its native kind or to its graph as
// Material.cpp decides (nativeKind(): native matte; native physicallyBased
// unless nativePhysicallyBasedCovers() refuses it; the graph for value types
// only it reads), as its DEBUG message at finalize() says after a render
bool testRouting(anari::Device d)
{
  struct Case
  {
    const char *name;
    anari::Material material;
    const char *expected;
  };
  std::vector<Case> cases;
  cases.push_back({"plain matte", matte(d, float3(0.5f)), "matte"});
  {
    auto m = anari::newObject<anari::Material>(d, "matte");
    anari::setParameter(d, m, "color", 0.4f);
    anari::commitParameters(d, m);
    cases.push_back({"matte with a FLOAT32 'color'", m, "C++"});
  }
  {
    auto m = physicallyBased(d, float3(0.8f, 0.5f, 0.3f), 0.5f, 0.4f);
    anari::commitParameters(d, m);
    cases.push_back({"plain physicallyBased", m, "physicallyBased"});
  }
  {
    std::vector<float4> texels(16, float4(0.3f, 0.6f, 0.9f, 1.f));
    auto m = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setAndReleaseParameter(
        d, m, "baseColor", image2D(d, texels, "linear"));
    anari::setAndReleaseParameter(
        d, m, "roughness", image2D(d, texels, "nearest"));
    anari::setParameter(d, m, "transmission", 0.5f);
    anari::setParameter(d, m, "clearcoat", 0.5f);
    anari::commitParameters(d, m);
    cases.push_back(
        {"textured physicallyBased with transmission and a "
         "clearcoat",
            m,
            "physicallyBased"});
  }
  {
    auto m = physicallyBased(d, float3(0.6f), 1.f, 0.3f);
    anari::setParameter(d, m, "iridescence", 1.f);
    anari::setParameter(d, m, "iridescenceThickness", 400.f);
    anari::commitParameters(d, m);
    cases.push_back({"iridescent physicallyBased", m, "C++"});
  }
  {
    auto m = anari::newObject<anari::Material>(d, "physicallyBased");
    anari::setParameter(d, m, "baseColor", 0.4f);
    anari::commitParameters(d, m);
    cases.push_back({"physicallyBased with a FLOAT32 'baseColor'", m, "C++"});
  }

  // one sphere each, rendered, which flushes the commits
  std::vector<anari::Surface> surfaces;
  for (size_t i = 0; i < cases.size(); i++) {
    addSurface(d,
        surfaces,
        sphere(d, float3(-0.75f + 0.3f * float(i), 0.f, 0.f), 0.12f),
        cases[i].material);
  }
  auto world = makeWorld(d, surfaces);
  RenderSettings settings;
  settings.size = uint2(16, 16);
  settings.pixelSamples = 1;
  render(d, world, true, settings);
  anari::release(d, world);

  bool pass = true;
  for (const Case &c : cases) {
    std::string got;
    {
      std::lock_guard<std::mutex> lock(g_routesMutex);
      const auto it = g_routes.find(c.material);
      got = it == g_routes.end() ? "no message" : it->second;
    }
    pass &= report(
        std::string("routing: ") + c.name + " is shaded by " + c.expected,
        got == c.expected,
        got);
  }
  return pass;
}

// --bench ////////////////////////////////////////////////////////////////////

double renderMilliseconds(anari::Device d, anari::Frame frame)
{
  const auto t0 = std::chrono::steady_clock::now();
  anari::render(d, frame);
  anari::wait(d, frame);
  return 1e3
      * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
}

void bench(anari::Device d, const std::string &scene, anari::World world)
{
  RenderSettings settings;
  settings.size = uint2(512, 512);
  settings.pixelSamples = 4;
  settings.maxBounces = 16;
  settings.albedoNormal = false;
  settings.accumulation = true;
  anari::Frame frames[2] = {makeFrame(d, world, true, settings),
      makeFrame(d, world, false, settings)};
  anari::release(d, world);

  // the first render of each builds the BVH and is left out; each later
  // one adds 'pixelSamples' samples, the same work both ways
  const int renders = 7;
  std::vector<double> times[2];
  for (int r = 0; r <= renders; r++) {
    for (int f = 0; f < 2; f++) {
      const double ms = renderMilliseconds(d, frames[f]);
      if (r > 0)
        times[f].push_back(ms);
    }
  }
  double median[2];
  for (int f = 0; f < 2; f++) {
    std::sort(times[f].begin(), times[f].end());
    median[f] = times[f][times[f].size() / 2];
    anari::release(d, frames[f]);
  }
  std::printf(
      "TIME %s, %ux%u, %d samples, maxBounces %d, median of %d renders: "
      "native %.1f ms, its graphs (nativeMaterials false) %.1f ms, speedup "
      "%.2fx\n",
      scene.c_str(),
      settings.size.x,
      settings.size.y,
      settings.pixelSamples,
      settings.maxBounces,
      renders,
      median[0],
      median[1],
      median[1] / median[0]);
}

} // namespace

int main(int argc, const char **argv)
{
  const bool benchmark = argc > 1 && std::strcmp(argv[1], "--bench") == 0;

  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = testRouting(d);
  pass &= testImages(d, "matte", makeMatteWorld(d), false);
  pass &= testImages(d, "physicallyBased", makePhysicallyBasedWorld(d), true);
  if (benchmark) {
    bench(d, "matte", makeMatteWorld(d));
    bench(d, "physicallyBased", makePhysicallyBasedWorld(d));
  }
  pass &= report("no warnings or errors", g_errors == 0);

  anari::release(d, d);
  anari::unloadLibrary(library);
  return pass ? 0 : 1;
}
