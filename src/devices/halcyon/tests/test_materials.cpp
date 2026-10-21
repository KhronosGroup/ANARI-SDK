// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Renders a quad facing an orthographic camera (so eye-lit shading gives the
// surface color exactly) with various materials and checks the float color
// channel: matte/physicallyBased colors, attribute colors, opacity and alpha
// modes (through 'opacityHeatmap'), invalid materials and surface visibility.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using namespace anari::math;

// not a multiple of any ISPC program count, to exercise partial SIMD chunks
constexpr uint2 IMAGE_SIZE = {37, 37};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
const float4 MAGENTA = {1.f, 0.f, 1.f, 1.f};
// matte's default colour, the specification's (helide's is white)
const float3 MATTE_DEFAULT = {0.8f, 0.8f, 0.8f};

// The quad covers [-0.8, 0.8]^2 at z = 0, facing +z; triangle 0 is the
// lower-right half (x > y), triangle 1 the upper-left
const std::vector<float3> VERTICES = {{-0.8f, -0.8f, 0.f},
    {0.8f, -0.8f, 0.f},
    {0.8f, 0.8f, 0.f},
    {-0.8f, 0.8f, 0.f}};
const std::vector<uint3> INDICES = {{0, 1, 2}, {0, 2, 3}};

const float2 TRI0_POS = {0.5f, -0.5f};
const float2 TRI1_POS = {-0.5f, 0.5f};
const float2 MISS_POS = {0.f, 0.95f};

using Setup = std::function<void(anari::Device, anari::Geometry)>;
using MaterialSetup = std::function<void(anari::Device, anari::Material)>;
using ObjectSetup = std::function<void(anari::Device, anari::Object)>;

// Scene description: a quad surface with a material of 'materialType'
// ("" for none) set up by 'material', optional geometry/surface/instance
// parameters. The material is never committed unless 'material' is set.
struct Scene
{
  const char *materialType{"matte"};
  MaterialSetup material;
  Setup geometry;
  ObjectSetup surface;
  ObjectSetup instance; // if set, the surface is placed in an instance
};

anari::World makeWorld(anari::Device d, const Scene &scene)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", VERTICES.data(), VERTICES.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", INDICES.data(), INDICES.size());
  if (scene.geometry)
    scene.geometry(d, geom);
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  if (*scene.materialType) {
    auto mat = anari::newObject<anari::Material>(d, scene.materialType);
    if (scene.material) {
      scene.material(d, mat);
      anari::commitParameters(d, mat);
    }
    anari::setAndReleaseParameter(d, surface, "material", mat);
  }
  if (scene.surface)
    scene.surface(d, surface);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  if (scene.instance) {
    auto group = anari::newObject<anari::Group>(d);
    anari::setParameterArray1D(d, group, "surface", &surface, 1);
    anari::commitParameters(d, group);
    auto inst = anari::newObject<anari::Instance>(d, "transform");
    anari::setAndReleaseParameter(d, inst, "group", group);
    scene.instance(d, inst);
    anari::commitParameters(d, inst);
    anari::setParameterArray1D(d, world, "instance", &inst, 1);
    anari::release(d, inst);
  } else {
    anari::setParameterArray1D(d, world, "surface", &surface, 1);
  }
  anari::commitParameters(d, world);
  anari::release(d, surface);
  return world;
}

// Orthographic camera looking down -z at the [-1, 1]^2 square
anari::Camera makeCamera(anari::Device d)
{
  auto camera = anari::newObject<anari::Camera>(d, "orthographic");
  anari::setParameter(d, camera, "position", float3(0.f, 0.f, 2.f));
  anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
  anari::setParameter(d, camera, "height", 2.f);
  anari::setParameter(d, camera, "aspect", 1.f);
  anari::commitParameters(d, camera);
  return camera;
}

// Renders 'world' in renderer 'mode' and returns the float color channel
std::vector<float4> render(
    anari::Device d, anari::World world, const char *mode)
{
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::setParameter(d, renderer, "mode", mode);
  anari::commitParameters(d, renderer);

  auto frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, frame, "size", IMAGE_SIZE);
  anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
  anari::setAndReleaseParameter(d, frame, "renderer", renderer);
  anari::setParameter(d, frame, "world", world);
  anari::commitParameters(d, frame);

  anari::render(d, frame);
  anari::wait(d, frame);

  std::vector<float4> pixels;
  auto fb = anari::map<float4>(d, frame, "channel.color");
  if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
    pixels.assign(fb.data, fb.data + fb.width * fb.height);
  else
    std::printf("FAIL %s: could not map channel.color\n", mode);
  anari::unmap(d, frame, "channel.color");
  anari::release(d, frame);
  return pixels;
}

size_t pixelIndex(float2 pos)
{
  const auto x = uint32_t((pos.x + 1.f) / 2.f * IMAGE_SIZE.x);
  const auto y = uint32_t((pos.y + 1.f) / 2.f * IMAGE_SIZE.y);
  return size_t(y) * IMAGE_SIZE.x + x;
}

bool near(float4 a, float4 b)
{
  const float4 d = linalg::abs(a - b);
  return d.x < 1e-4f && d.y < 1e-4f && d.z < 1e-4f && d.w < 1e-4f;
}

bool checkPixel(const char *test,
    const char *what,
    const std::vector<float4> &pixels,
    float2 pos,
    float4 expected)
{
  if (pixels.empty())
    return false;
  const float4 got = pixels[pixelIndex(pos)];
  if (near(got, expected))
    return true;
  std::printf("FAIL %s, %s: got (%f, %f, %f, %f), expected (%f, %f, %f, %f)\n",
      test,
      what,
      got.x,
      got.y,
      got.z,
      got.w,
      expected.x,
      expected.y,
      expected.z,
      expected.w);
  return false;
}

// Renders 'scene' in 'mode' and checks both triangles and a missed pixel
bool check(anari::Device d,
    const char *test,
    const Scene &scene,
    const char *mode,
    float4 expectedTri0,
    float4 expectedTri1)
{
  auto world = makeWorld(d, scene);
  const auto pixels = render(d, world, mode);
  anari::release(d, world);

  bool pass = checkPixel(test, "triangle 0", pixels, TRI0_POS, expectedTri0);
  pass &= checkPixel(test, "triangle 1", pixels, TRI1_POS, expectedTri1);
  pass &= checkPixel(test, "miss", pixels, MISS_POS, BACKGROUND);
  std::printf("%s %s\n", pass ? "PASS" : "FAIL", test);
  return pass;
}

// Face-on eye-lit shading is the color itself; the pixel is opaque
bool checkColor(
    anari::Device d, const char *test, const Scene &scene, float3 c0, float3 c1)
{
  return check(d, test, scene, "default", float4(c0, 1.f), float4(c1, 1.f));
}

bool checkColor(anari::Device d, const char *test, const Scene &scene, float3 c)
{
  return checkColor(d, test, scene, c, c);
}

// helide's opacity heatmap (blue, red, yellow, white), face-on
float3 heatmap(float o)
{
  const float3 stops[] = {
      {0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f}, {1.f, 1.f, 1.f}};
  const float f = linalg::clamp(o, 0.f, 1.f) * 3.f;
  const int i = f >= 3.f ? 2 : int(f);
  return linalg::lerp(stops[i], stops[i + 1], f - i);
}

bool checkOpacity(
    anari::Device d, const char *test, const Scene &scene, float o)
{
  const float4 c(heatmap(o), 1.f);
  return check(d, test, scene, "opacityHeatmap", c, c);
}

MaterialSetup matte(float3 color)
{
  return [=](anari::Device d, anari::Material m) {
    anari::setParameter(d, m, "color", color);
  };
}

// matte with 'opacity' and 'alphaMode'
MaterialSetup translucent(float opacity, const char *alphaMode)
{
  return [=](anari::Device d, anari::Material m) {
    anari::setParameter(d, m, "opacity", opacity);
    anari::setParameter(d, m, "alphaMode", alphaMode);
  };
}

const std::vector<float3> PRIMITIVE_COLORS = {
    {0.9f, 0.1f, 0.2f}, {0.3f, 0.7f, 0.4f}};

void setPrimitiveColors(anari::Device d, anari::Geometry g)
{
  anari::setParameterArray1D(d,
      g,
      "primitive.color",
      PRIMITIVE_COLORS.data(),
      PRIMITIVE_COLORS.size());
}

bool testColors(anari::Device d)
{
  bool pass = true;

  // the specification's defaults: matte grey 0.8 (helide's is white),
  // physicallyBased white, also for materials that are never committed
  pass &= checkColor(d, "matte defaults", {"matte"}, MATTE_DEFAULT);
  pass &= checkColor(d,
      "matte defaults, committed",
      {"matte", [](anari::Device, anari::Material) {}},
      MATTE_DEFAULT);
  pass &= checkColor(
      d, "physicallyBased defaults", {"physicallyBased"}, float3(1.f));
  pass &= checkColor(d,
      "physicallyBased defaults, committed",
      {"physicallyBased", [](anari::Device, anari::Material) {}},
      float3(1.f));
  // unsetting the colour returns to the default
  pass &= checkColor(d,
      "matte color unset",
      {"matte",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "color", float3(0.2f, 0.4f, 0.6f));
            anari::commitParameters(d, m);
            anari::unsetParameter(d, m, "color");
          }},
      MATTE_DEFAULT);

  pass &= checkColor(d,
      "matte color float3",
      {"matte", matte(float3(0.2f, 0.4f, 0.6f))},
      float3(0.2f, 0.4f, 0.6f));
  pass &= checkColor(d,
      "matte color float4",
      {"matte",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "color", float4(0.6f, 0.4f, 0.2f, 0.5f));
          }},
      float3(0.6f, 0.4f, 0.2f));
  // each subtype reads only its own color parameter
  pass &= checkColor(d,
      "matte ignores baseColor",
      {"matte",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "baseColor", float3(0.1f));
          }},
      MATTE_DEFAULT);
  pass &= checkColor(d,
      "physicallyBased baseColor",
      {"physicallyBased",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "color", float3(0.9f));
            anari::setParameter(d, m, "baseColor", float3(0.3f, 0.5f, 0.7f));
          }},
      float3(0.3f, 0.5f, 0.7f));

  // attribute colors
  auto colorFrom = [](const char *attribute) {
    return [=](anari::Device d, anari::Material m) {
      anari::setParameter(d, m, "color", attribute);
    };
  };
  pass &= checkColor(d,
      "color from primitive.color",
      {"matte", colorFrom("color"), setPrimitiveColors},
      PRIMITIVE_COLORS[0],
      PRIMITIVE_COLORS[1]);
  pass &= checkColor(d,
      "baseColor from attribute0",
      {"physicallyBased",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "baseColor", "attribute0");
          },
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(
                d, g, "attribute0", float4(0.25f, 0.5f, 0.75f, 1.f));
          }},
      float3(0.25f, 0.5f, 0.75f));
  pass &= checkColor(d,
      "color from an unset attribute",
      {"matte", colorFrom("attribute2")},
      float3(0.f));
  pass &= checkColor(d,
      "color from worldNormal",
      {"matte", colorFrom("worldNormal")},
      float3(0.f, 0.f, 1.f));
  // an instance's uniform attribute overrides the geometry's (uniform
  // attributes are float4 only, as in helide)
  pass &= checkColor(d,
      "color from instance color",
      {"matte",
          colorFrom("color"),
          setPrimitiveColors,
          nullptr,
          [](anari::Device d, anari::Object i) {
            anari::setParameter(d, i, "color", float4(0.5f, 0.6f, 0.7f, 1.f));
          }},
      float3(0.5f, 0.6f, 0.7f));
  // "none" is the constant color
  pass &= checkColor(d,
      "color none",
      {"matte", colorFrom("none"), setPrimitiveColors},
      MATTE_DEFAULT);

  return pass;
}

bool testOpacity(anari::Device d)
{
  bool pass = true;

  pass &= checkOpacity(d, "default opacity", {"matte"}, 1.f);
  pass &= checkOpacity(
      d, "opaque ignores opacity", {"matte", translucent(0.3f, "opaque")}, 1.f);
  pass &= checkOpacity(
      d, "blend opacity", {"matte", translucent(0.5f, "blend")}, 0.5f);
  pass &= checkOpacity(
      d, "mask below cutoff", {"matte", translucent(0.4f, "mask")}, 0.f);
  pass &= checkOpacity(
      d, "mask at cutoff", {"matte", translucent(0.5f, "mask")}, 1.f);
  pass &= checkOpacity(d,
      "mask alphaCutoff",
      {"matte",
          [](anari::Device d, anari::Material m) {
            translucent(0.7f, "mask")(d, m);
            anari::setParameter(d, m, "alphaCutoff", 0.8f);
          }},
      0.f);
  // the color's alpha scales the opacity
  pass &= checkOpacity(d,
      "blend color alpha",
      {"physicallyBased",
          [](anari::Device d, anari::Material m) {
            translucent(0.5f, "blend")(d, m);
            anari::setParameter(d, m, "baseColor", float4(1.f, 1.f, 1.f, 0.5f));
          }},
      0.25f);
  pass &= checkOpacity(d,
      "blend opacity from attribute1",
      {"matte",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "alphaMode", "blend");
            anari::setParameter(d, m, "opacity", "attribute1");
          },
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(
                d, g, "attribute1", float4(0.75f, 0.f, 0.f, 1.f));
          }},
      0.75f);

  // Unlike helide, the default mode blends the surface over the background
  // by its opacity (test_transparency covers layers, masks and ids)
  pass &= checkColor(d,
      "blend pixel over background",
      {"matte",
          [](anari::Device d, anari::Material m) {
            translucent(0.25f, "blend")(d, m);
            anari::setParameter(d, m, "color", float3(0.5f));
          }},
      0.25f * float3(0.5f)
          + 0.75f * float3(BACKGROUND.x, BACKGROUND.y, BACKGROUND.z));

  return pass;
}

bool testInvalidMaterials(anari::Device d)
{
  bool pass = true;

  pass &= check(d, "missing material", {""}, "default", MAGENTA, MAGENTA);
  pass &= check(d,
      "unknown material subtype",
      {"no such material"},
      "default",
      MAGENTA,
      MAGENTA);
  const float4 blue(heatmap(0.f), 1.f);
  pass &=
      check(d, "missing material opacity", {""}, "opacityHeatmap", blue, blue);

  anari::setParameter(
      d, d, "invalidMaterialColor", float4(0.f, 1.f, 0.f, 0.5f));
  anari::commitParameters(d, d);
  const float4 green(0.f, 1.f, 0.f, 1.f);
  pass &= check(d, "invalidMaterialColor", {""}, "default", green, green);

  // Disallowing invalid materials drops such surfaces from existing worlds
  auto world = makeWorld(d, {""});
  auto checkWorld = [&](const char *test, float4 expected) {
    const auto pixels = render(d, world, "default");
    const bool p = checkPixel(test, "hit", pixels, TRI0_POS, expected);
    std::printf("%s %s\n", p ? "PASS" : "FAIL", test);
    return p;
  };
  anari::setParameter(d, d, "allowInvalidMaterials", false);
  anari::commitParameters(d, d);
  pass &= checkWorld("allowInvalidMaterials false", BACKGROUND);
  anari::setParameter(d, d, "allowInvalidMaterials", true);
  anari::commitParameters(d, d);
  pass &= checkWorld("allowInvalidMaterials true", green);
  anari::release(d, world);

  anari::setParameter(d, d, "invalidMaterialColor", MAGENTA);
  anari::commitParameters(d, d);

  return pass;
}

// A device commit is a change: rendering the same, otherwise unchanged frames
// again shows the new device parameters. Two frames share the world, and the
// second is rendered after the first, which puts the parameters into effect.
bool testDeviceCommitRerenders(anari::Device d)
{
  auto world = makeWorld(d, {""});
  auto renderer = anari::newObject<anari::Renderer>(d, "preview");
  anari::setParameter(d, renderer, "background", BACKGROUND);
  anari::commitParameters(d, renderer);

  anari::Frame frames[2];
  for (auto &frame : frames) {
    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    anari::setAndReleaseParameter(d, frame, "camera", makeCamera(d));
    anari::setParameter(d, frame, "renderer", renderer);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }
  anari::release(d, renderer);
  anari::release(d, world);

  auto checkFrames = [&](const char *test, float4 expected) {
    bool p = true;
    for (auto frame : frames) {
      anari::render(d, frame);
      anari::wait(d, frame);
      std::vector<float4> pixels;
      auto fb = anari::map<float4>(d, frame, "channel.color");
      if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
        pixels.assign(fb.data, fb.data + fb.width * fb.height);
      anari::unmap(d, frame, "channel.color");
      p &= checkPixel(test,
          frame == frames[0] ? "frame 0 hit" : "frame 1 hit",
          pixels,
          TRI0_POS,
          expected);
    }
    std::printf("%s %s\n", p ? "PASS" : "FAIL", test);
    return p;
  };

  const float4 green(0.f, 1.f, 0.f, 1.f);
  bool pass = checkFrames("rerender: initial invalid color", MAGENTA);

  anari::setParameter(d, d, "invalidMaterialColor", green);
  anari::commitParameters(d, d);
  pass &= checkFrames("rerender: invalidMaterialColor", green);

  anari::setParameter(d, d, "allowInvalidMaterials", false);
  anari::commitParameters(d, d);
  pass &= checkFrames("rerender: allowInvalidMaterials false", BACKGROUND);

  anari::setParameter(d, d, "allowInvalidMaterials", true);
  anari::commitParameters(d, d);
  pass &= checkFrames("rerender: allowInvalidMaterials true", green);

  anari::setParameter(d, d, "invalidMaterialColor", MAGENTA);
  anari::commitParameters(d, d);
  pass &= checkFrames("rerender: invalidMaterialColor restored", MAGENTA);

  for (auto frame : frames)
    anari::release(d, frame);
  return pass;
}

// openPBR and usdPreviewSurface (MaterialX's models) are their diffuse
// colour (a color3: its alpha isn't opacity) and their opacity here, which
// blends; usdPreviewSurface's opacityThreshold makes it a mask
bool testMaterialModels(anari::Device d, std::vector<std::string> &warnings)
{
  bool pass = true;

  // their defaults, also for materials that are never committed
  pass &= checkColor(d, "openPBR defaults", {"openPBR"}, float3(0.8f));
  pass &= checkColor(
      d, "usdPreviewSurface defaults", {"usdPreviewSurface"}, float3(0.18f));
  pass &= checkColor(d,
      "openPBR committed defaults",
      {"openPBR", [](anari::Device, anari::Material) {}},
      float3(0.8f));
  pass &= checkColor(d,
      "usdPreviewSurface committed defaults",
      {"usdPreviewSurface", [](anari::Device, anari::Material) {}},
      float3(0.18f));

  pass &= checkColor(d,
      "openPBR base_color",
      {"openPBR",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "color", float3(0.9f));
            anari::setParameter(d, m, "baseColor", float3(0.9f));
            anari::setParameter(d, m, "base_color", float3(0.3f, 0.5f, 0.7f));
          }},
      float3(0.3f, 0.5f, 0.7f));
  pass &= checkColor(d,
      "usdPreviewSurface diffuseColor from primitive.color",
      {"usdPreviewSurface",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "diffuseColor", "color");
          },
          setPrimitiveColors},
      PRIMITIVE_COLORS[0],
      PRIMITIVE_COLORS[1]);

  const auto set = [](const char *name, auto value) {
    return [=](anari::Device d, anari::Material m) {
      anari::setParameter(d, m, name, value);
    };
  };
  pass &= checkOpacity(d,
      "openPBR geometry_opacity blends",
      {"openPBR", set("geometry_opacity", 0.5f)},
      0.5f);
  pass &= checkOpacity(d,
      "openPBR ignores 'opacity' and 'alphaMode'",
      {"openPBR",
          [](anari::Device d, anari::Material m) {
            translucent(0.3f, "blend")(d, m);
          }},
      1.f);
  pass &= checkOpacity(d,
      "usdPreviewSurface opacity blends",
      {"usdPreviewSurface", set("opacity", 0.4f)},
      0.4f);
  pass &= checkOpacity(d,
      "usdPreviewSurface opacityThreshold: below",
      {"usdPreviewSurface",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "opacity", 0.4f);
            anari::setParameter(d, m, "opacityThreshold", 0.5f);
          }},
      0.f);
  pass &= checkOpacity(d,
      "usdPreviewSurface opacityThreshold: at",
      {"usdPreviewSurface",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "opacity", 0.5f);
            anari::setParameter(d, m, "opacityThreshold", 0.5f);
          }},
      1.f);
  // the colour's alpha doesn't scale the opacity
  pass &= checkOpacity(d,
      "usdPreviewSurface ignores the colour's alpha",
      {"usdPreviewSurface",
          [](anari::Device d, anari::Material m) {
            anari::setParameter(d, m, "diffuseColor", "attribute0");
            anari::setParameter(d, m, "opacity", 0.5f);
          },
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(
                d, g, "attribute0", float4(0.2f, 0.4f, 0.6f, 0.25f));
          }},
      0.5f);
  pass &= checkOpacity(d,
      "openPBR opacity from attribute1",
      {"openPBR",
          set("geometry_opacity", "attribute1"),
          [](anari::Device d, anari::Geometry g) {
            anari::setParameter(
                d, g, "attribute1", float4(0.75f, 0.f, 0.f, 1.f));
          }},
      0.75f);

  warnings.clear();
  pass &= checkOpacity(d,
      "usdPreviewSurface unknown opacityMode",
      {"usdPreviewSurface", set("opacityMode", "glass")},
      1.f);
  bool warned = false;
  for (const auto &w : warnings)
    warned = warned || w.find("opacityMode 'glass'") != std::string::npos;
  std::printf("%s usdPreviewSurface warns of an unknown opacityMode\n",
      warned ? "PASS" : "FAIL");
  pass &= warned;

  // the nodedef's integer form: 0 and 1 are the enum's, others warn
  warnings.clear();
  checkOpacity(d,
      "usdPreviewSurface opacityMode 1",
      {"usdPreviewSurface", set("opacityMode", int32_t(1))},
      1.f);
  checkOpacity(d,
      "usdPreviewSurface opacityMode 2",
      {"usdPreviewSurface", set("opacityMode", int32_t(2))},
      1.f);
  warned = warnings.size() == 1
      && warnings[0].find("opacityMode '2'") != std::string::npos;
  std::printf("%s usdPreviewSurface takes opacityMode 0/1 and warns of 2\n",
      warned ? "PASS" : "FAIL");
  pass &= warned;
  return pass;
}

bool testVisibility(anari::Device d)
{
  auto visible = [](bool v) {
    return [=](anari::Device d, anari::Object s) {
      anari::setParameter(d, s, "visible", v);
    };
  };
  bool pass = true;
  pass &= checkColor(
      d, "visible", {"matte", nullptr, nullptr, visible(true)}, MATTE_DEFAULT);
  pass &= check(d,
      "invisible",
      {"matte", nullptr, nullptr, visible(false)},
      "default",
      BACKGROUND,
      BACKGROUND);
  return pass;
}

// Introspection declares matte's default colour as the one it renders
bool testMatteDefaultDeclared(anari::Device d)
{
  auto *c = (const float *)anariGetParameterInfo(d,
      ANARI_MATERIAL,
      "matte",
      "color",
      ANARI_FLOAT32_VEC3,
      "default",
      ANARI_FLOAT32_VEC3);
  const bool pass = c && c[0] == MATTE_DEFAULT.x && c[1] == MATTE_DEFAULT.y
      && c[2] == MATTE_DEFAULT.z;
  std::printf(
      "%s matte color default declared as rendered\n", pass ? "PASS" : "FAIL");
  return pass;
}

// device errors (e.g. ISPC object size mismatches) fail the test; warnings
// are recorded
bool g_deviceError = false;
std::vector<std::string> g_warnings;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_ERROR) {
    g_deviceError = true;
    std::fprintf(stderr, "[ANARI] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    g_warnings.push_back(message);
  }
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

  bool pass = true;
  pass &= testColors(d);
  pass &= testOpacity(d);
  pass &= testInvalidMaterials(d);
  pass &= testDeviceCommitRerenders(d);
  pass &= testVisibility(d);
  pass &= testMatteDefaultDeclared(d);
  pass &= testMaterialModels(d, g_warnings);

  anari::release(d, d);
  anari::unloadLibrary(library);

  if (g_deviceError)
    std::printf("FAIL: device reported errors\n");

  return pass && !g_deviceError ? 0 : 1;
}
