// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The 'materialx' material subtype (HALCYON_MATERIAL_MATERIALX, VisRTX's
// contract) through the device: a floor seen from straight above under a
// directional light of irradiance pi straight down, with no bounces, so that
// a diffuse floor shows its albedo exactly.
//
// - introspection lists the subtype and the extension;
// - inline and file documents, materialName, and the materialNames and
//   textureInputs properties (only after a successful commit);
// - overrides: a value and a sampler on a shader input, through both
//   renderers; unknown names and types warn;
// - geomprop nodes (geompropvalue, UsdPrimvarReader) read an attribute the
//   hit has (vertex, primitive, uniform, instance, ray-derived; lane by lane
//   in one frame), else their default (integer and boolean nodes too);
//   an attribute override reads (0, 0, 0, 1) where the hit lacks it, in both
//   renderers;
// - a document that doesn't read, a material it doesn't have and a graph that
//   doesn't compile are ERRORs and make the material invalid, so surfaces
//   show invalidMaterialColor (or nothing, with allowInvalidMaterials
//   false), until a commit fixes it;
// - the 'preview' renderer shows the shader's diffuse colour.

// anari
#define ANARI_LINALG_DEFINITIONS
#include <anari/anari_cpp/ext/linalg.h>
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
// POSIX
#include <unistd.h>

namespace {

using namespace anari::math;
namespace fs = std::filesystem;

constexpr float PI = 3.14159265358979323846f;
constexpr uint2 IMAGE_SIZE = {16, 16};
const float4 BACKGROUND = {0.1f, 0.2f, 0.3f, 1.f};
const float4 INVALID = {0.f, 1.f, 0.f, 1.f};

const fs::path EXAMPLES =
    fs::path(HALCYON_MATERIALX_RESOURCES) / "Materials" / "Examples";

std::vector<std::string> g_errors;
std::vector<std::string> g_warnings;
std::vector<std::string> g_debug;

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
    g_errors.push_back(message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    g_warnings.push_back(message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    g_debug.push_back(message);
  }
}

void clearMessages()
{
  g_errors.clear();
  g_warnings.clear();
  g_debug.clear();
}

bool contains(const std::vector<std::string> &messages, const std::string &s)
{
  for (const auto &m : messages) {
    if (m.find(s) != std::string::npos)
      return true;
  }
  return false;
}

void printMessages()
{
  for (const auto &m : g_errors)
    std::printf("    ERROR: %s\n", m.c_str());
  for (const auto &m : g_warnings)
    std::printf("    WARNING: %s\n", m.c_str());
}

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok) {
    printMessages();
    g_failures++;
  }
}

// A standard_surface that is a Lambertian of 'color' (base 1, no specular)
std::string lambertian(const char *color, const char *name = "M")
{
  return std::string(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <standard_surface name="S" type="surfaceshader">
    <input name="base" type="float" value="1" />
    <input name="base_color" type="color3" value=")")
      + color + R"(" />
    <input name="specular" type="float" value="0" />
  </standard_surface>
  <surfacematerial name=")"
      + name + R"(" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)";
}

anari::Material inlineMaterial(anari::Device d, const std::string &xml)
{
  auto m = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, m, "sourceType", "documentInline");
  anari::setParameter(d, m, "source", xml);
  anari::commitParameters(d, m);
  return m;
}

// The floor [-1, 1]^2 at z = 0, facing +z, lit straight down (irradiance pi);
// with 'normal', its shading normal
anari::World floorWorld(
    anari::Device d, anari::Material mat, const float3 *normal = nullptr)
{
  const std::vector<float3> v = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const std::vector<float2> uv = {
      {0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "vertex.attribute0", uv.data(), uv.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  if (normal) {
    const std::vector<float3> n(v.size(), *normal);
    anari::setParameterArray1D(d, geom, "vertex.normal", n.data(), n.size());
  }
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, light, "irradiance", PI);
  anari::commitParameters(d, light);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::release(d, light);
  anari::commitParameters(d, world);
  return world;
}

// A frame of 'world' through an orthographic camera looking down at the
// floor; the pathtracer without bounces after the first vertex
struct Scene
{
  anari::Device d;
  anari::Frame frame;

  Scene(anari::Device device, anari::World world, const char *renderer)
      : d(device)
  {
    auto r = anari::newObject<anari::Renderer>(d, renderer);
    // one render is the whole first render (no progressive resolution)
    anari::setParameter(d, r, "resolutionDivider", 0);
    anari::setParameter(d, r, "background", BACKGROUND);
    anari::setParameter(d, r, "maxBounces", 0);
    anari::setParameter(d, r, "pixelSamples", 1);
    anari::setParameter(d, r, "ambientOcclusion", false);
    anari::commitParameters(d, r);

    auto camera = anari::newObject<anari::Camera>(d, "orthographic");
    anari::setParameter(d, camera, "position", float3(0.f, 0.f, 3.f));
    anari::setParameter(d, camera, "direction", float3(0.f, 0.f, -1.f));
    anari::setParameter(d, camera, "up", float3(0.f, 1.f, 0.f));
    anari::setParameter(d, camera, "height", 2.f);
    anari::setParameter(d, camera, "aspect", 1.f);
    anari::commitParameters(d, camera);

    frame = anari::newObject<anari::Frame>(d);
    anari::setParameter(d, frame, "size", IMAGE_SIZE);
    anari::setParameter(d, frame, "channel.color", ANARI_FLOAT32_VEC4);
    anari::setAndReleaseParameter(d, frame, "camera", camera);
    anari::setAndReleaseParameter(d, frame, "renderer", r);
    anari::setParameter(d, frame, "world", world);
    anari::commitParameters(d, frame);
  }

  ~Scene()
  {
    anari::release(d, frame);
  }

  // The centre pixel
  float4 render()
  {
    const std::vector<float4> image = renderImage();
    return image.empty()
        ? float4(-1.f)
        : image[(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + IMAGE_SIZE.x / 2 + 2];
  }

  // Every pixel, row by row; empty if the frame doesn't map
  std::vector<float4> renderImage()
  {
    anari::render(d, frame);
    anari::wait(d, frame);
    std::vector<float4> image;
    auto fb = anari::map<float4>(d, frame, "channel.color");
    if (fb.data && fb.width == IMAGE_SIZE.x && fb.height == IMAGE_SIZE.y)
      image.assign(fb.data, fb.data + IMAGE_SIZE.x * IMAGE_SIZE.y);
    anari::unmap(d, frame, "channel.color");
    return image;
  }
};

bool near(const float4 &a, const float4 &b, float eps = 1e-3f)
{
  bool ok = true;
  for (int c = 0; c < 3; c++)
    ok = ok && std::abs(a[c] - b[c]) <= eps;
  if (!ok) {
    std::printf("    got (%g, %g, %g), expected (%g, %g, %g)\n",
        a.x,
        a.y,
        a.z,
        b.x,
        b.y,
        b.z);
  }
  return ok;
}

std::vector<std::string> stringList(
    anari::Device d, anari::Material m, const char *name, bool *found)
{
  const char **list = nullptr;
  *found = anariGetProperty(
      d, m, name, ANARI_STRING_LIST, &list, sizeof(list), ANARI_WAIT);
  std::vector<std::string> result;
  for (const char **s = list; *found && s && *s; s++)
    result.push_back(*s);
  return result;
}

bool valid(anari::Device d, anari::Material m)
{
  bool v = false;
  anariGetProperty(d, m, "valid", ANARI_BOOL, &v, sizeof(v), ANARI_WAIT);
  return v;
}

// Tests //////////////////////////////////////////////////////////////////////

void testIntrospection(anari::Library lib, anari::Device d)
{
  bool subtype = false;
  for (const char **s = anariGetObjectSubtypes(d, ANARI_MATERIAL); s && *s; s++)
    subtype = subtype || std::strcmp(*s, "materialx") == 0;
  bool extension = false;
  for (const char **e = anariGetDeviceExtensions(lib, "default"); e && *e; e++)
    extension =
        extension || std::strcmp(*e, "ANARI_HALCYON_MATERIAL_MATERIALX") == 0;
  check(subtype && extension,
      "the device lists the materialx subtype and HALCYON_MATERIAL_MATERIALX");
}

void testInline(anari::Device d)
{
  clearMessages();
  auto m = inlineMaterial(d, lambertian("0.8, 0.4, 0.2"));
  auto world = floorWorld(d, m);
  {
    Scene pt(d, world, "quality");
    Scene def(d, world, "default");
    check(near(pt.render(), float4(0.8f, 0.4f, 0.2f, 1.f))
            && near(def.render(), float4(0.8f, 0.4f, 0.2f, 1.f))
            && g_errors.empty() && g_warnings.empty() && valid(d, m),
        "an inline document: the pathtracer shades its graph, the default "
        "renderer shows its base colour");
    check(contains(g_debug, "through its MaterialXCpp graph in C++"),
        "the pathtracer shades it through its graph");

    bool found = false;
    const auto names = stringList(d, m, "materialNames", &found);
    const char **again = nullptr;
    anariGetProperty(
        d, m, "materialNames", ANARI_STRING_LIST, &again, sizeof(again), 0);
    stringList(d, m, "materialNames", &found);
    check(names == std::vector<std::string>{"M"} && found && again && again[0]
            && std::string(again[0]) == "M",
        "materialNames lists the document's materials, and a list stays "
        "valid over later queries");
    check(stringList(d, m, "textureInputs", &found).empty() && found,
        "textureInputs: none in this document");

    // overrides: a value, then a sampler
    clearMessages();
    anari::setParameter(d, m, "base_color", float3(0.25f, 0.5f, 0.75f));
    anari::commitParameters(d, m);
    check(near(pt.render(), float4(0.25f, 0.5f, 0.75f, 1.f))
            && near(def.render(), float4(0.25f, 0.5f, 0.75f, 1.f))
            && g_warnings.empty(),
        "a value overrides the shader input of its name, in both renderers");

    const float4 texel(0.1f, 0.6f, 0.3f, 1.f);
    auto image = anari::newArray2D(d, &texel, 1, 1);
    auto sampler = anari::newObject<anari::Sampler>(d, "image2D");
    anari::setAndReleaseParameter(d, sampler, "image", image);
    anari::setParameter(d, sampler, "inAttribute", "attribute0");
    anari::commitParameters(d, sampler);
    anari::setParameter(d, m, "base_color", sampler);
    anari::commitParameters(d, m);
    check(near(pt.render(), texel) && near(def.render(), texel)
            && g_warnings.empty(),
        "a sampler overrides a shader input, in both renderers");
    anari::release(d, sampler);

    clearMessages();
    anari::unsetParameter(d, m, "base_color");
    anari::setParameter(d, m, "no_such_input", 1.f);
    anari::setParameter(d, m, "base", float3(1.f));
    anari::commitParameters(d, m);
    check(near(pt.render(), float4(0.8f, 0.4f, 0.2f, 1.f))
            && contains(g_warnings, "'no_such_input' is neither an input of")
            && contains(g_warnings, "input 'base' is a float"),
        "unknown names and mismatched types warn and are ignored");
  }
  anari::release(d, world);
  anari::release(d, m);
}

// An open_pbr_surface that is a Lambertian of 'color' (no specular)
std::string openPbrLambertian(const char *color)
{
  return std::string(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <open_pbr_surface name="S" type="surfaceshader">
    <input name="base_color" type="color3" value=")")
      + color + R"(" />
    <input name="specular_weight" type="float" value="0" />
  </open_pbr_surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)";
}

// An invalid sampler override counts as unset: the input keeps what the
// document gives it, its geomprop for a defaultgeomprop input (the shader's
// normal, Nworld: the floor's shading normal, tilted to cos 0.8), its value
// for others
void testInvalidSamplerOverride(
    anari::Device d, const std::string &xml, const char *normalInput)
{
  clearMessages();
  auto m = inlineMaterial(d, xml);
  const float3 tilted(0.6f, 0.f, 0.8f);
  auto world = floorWorld(d, m, &tilted);
  {
    Scene pt(d, world, "quality");
    const float4 lit(0.64f, 0.32f, 0.16f, 1.f);
    const std::string model = std::string(normalInput) + ": ";
    check(near(pt.render(), lit), model + "the document's normal is Nworld");

    auto invalid = anari::newObject<anari::Sampler>(d, "image2D");
    anari::commitParameters(d, invalid);
    anari::setParameter(d, m, normalInput, invalid);
    anari::setParameter(d, m, "base_color", invalid);
    anari::commitParameters(d, m);
    check(near(pt.render(), lit) && g_warnings.empty() && g_errors.empty(),
        model
            + "an invalid sampler override is unset: the normal stays Nworld, "
              "the base colour the document's");

    // valid again: the override takes effect
    const float4 texel(0.5f, 0.5f, 0.5f, 1.f);
    auto image = anari::newArray2D(d, &texel, 1, 1);
    anari::setAndReleaseParameter(d, invalid, "image", image);
    anari::commitParameters(d, invalid);
    anari::unsetParameter(d, m, normalInput);
    anari::commitParameters(d, m);
    check(near(pt.render(), float4(0.4f, 0.4f, 0.4f, 1.f)),
        model + "the sampler turned valid overrides the input");
    anari::release(d, invalid);
  }
  anari::release(d, world);
  anari::release(d, m);
}

// Where the floor's attribute comes from
enum class Source
{
  NONE,
  VERTEX,
  PRIMITIVE,
  UNIFORM,
  INSTANCE
};

// floorWorld()'s floor and light, the floor in an instance, with attribute
// 'attribute' of 'value' from 'source'
anari::World attributeFloor(anari::Device d,
    anari::Material mat,
    const char *attribute,
    Source source,
    float4 value)
{
  const std::vector<float3> v = {
      {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
  const std::vector<float4> values(v.size(), value);
  const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(d, geom, "vertex.position", v.data(), v.size());
  anari::setParameterArray1D(
      d, geom, "primitive.index", indices.data(), indices.size());
  if (source == Source::VERTEX) {
    anari::setParameterArray1D(d,
        geom,
        (std::string("vertex.") + attribute).c_str(),
        values.data(),
        values.size());
  } else if (source == Source::PRIMITIVE) {
    anari::setParameterArray1D(d,
        geom,
        (std::string("primitive.") + attribute).c_str(),
        values.data(),
        indices.size());
  } else if (source == Source::UNIFORM) {
    anari::setParameter(d, geom, attribute, value);
  }
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);
  auto group = anari::newObject<anari::Group>(d);
  anari::setParameterArray1D(d, group, "surface", &surface, 1);
  anari::release(d, surface);
  anari::commitParameters(d, group);
  auto instance = anari::newObject<anari::Instance>(d, "transform");
  anari::setAndReleaseParameter(d, instance, "group", group);
  if (source == Source::INSTANCE)
    anari::setParameter(d, instance, attribute, value);
  anari::commitParameters(d, instance);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "instance", &instance, 1);
  anari::release(d, instance);
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, light, "irradiance", PI);
  anari::commitParameters(d, light);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::release(d, light);
  anari::commitParameters(d, world);
  return world;
}

// floorWorld()'s floor and light as IMAGE_SIZE.x strips along y, each a
// pixel of Scene's image wide, the even ones with vertex.attribute1 of
// 'value', the odd ones without
anari::World stripsWorld(anari::Device d, anari::Material mat, float4 value)
{
  std::vector<anari::Surface> surfaces;
  const float w = 2.f / IMAGE_SIZE.x;
  for (uint32_t i = 0; i < IMAGE_SIZE.x; i++) {
    const float x0 = -1.f + i * w, x1 = x0 + w;
    const std::vector<float3> v = {
        {x0, -1.f, 0.f}, {x1, -1.f, 0.f}, {x1, 1.f, 0.f}, {x0, 1.f, 0.f}};
    const std::vector<float4> values(v.size(), value);
    const std::vector<uint3> indices = {{0, 1, 2}, {0, 2, 3}};
    auto geom = anari::newObject<anari::Geometry>(d, "triangle");
    anari::setParameterArray1D(
        d, geom, "vertex.position", v.data(), v.size());
    anari::setParameterArray1D(
        d, geom, "primitive.index", indices.data(), indices.size());
    if (i % 2 == 0) {
      anari::setParameterArray1D(
          d, geom, "vertex.attribute1", values.data(), values.size());
    }
    anari::commitParameters(d, geom);
    auto surface = anari::newObject<anari::Surface>(d);
    anari::setAndReleaseParameter(d, surface, "geometry", geom);
    anari::setParameter(d, surface, "material", mat);
    anari::commitParameters(d, surface);
    surfaces.push_back(surface);
  }

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(
      d, world, "surface", surfaces.data(), surfaces.size());
  for (anari::Surface s : surfaces)
    anari::release(d, s);
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(d, light, "direction", float3(0.f, 0.f, -1.f));
  anari::setParameter(d, light, "irradiance", PI);
  anari::commitParameters(d, light);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::release(d, light);
  anari::commitParameters(d, world);
  return world;
}

// The floor's colour through each renderer with attribute 'attribute' of
// 'value' from 'source'
struct Pixels
{
  float4 pathtracer;
  float4 standard; // the 'preview' renderer
};

Pixels renderFloor(anari::Device d,
    anari::Material m,
    const char *attribute,
    Source source,
    float4 value)
{
  auto world = attributeFloor(d, m, attribute, source, value);
  Pixels p;
  {
    Scene pt(d, world, "quality");
    Scene def(d, world, "default");
    p.pathtracer = pt.render();
    p.standard = def.render();
  }
  anari::release(d, world);
  return p;
}

// A Lambertian standard_surface whose base colour is a geompropvalue node
// reading 'geomprop' of default (0.2, 0.6, 0.4), and whose base weight is a
// UsdPrimvarReader of 'attribute2' of fallback 0.5
std::string geompropDocument(const char *geomprop)
{
  return std::string(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <geompropvalue name="primvar" type="color3">
    <input name="geomprop" type="string" value=")")
      + geomprop + R"(" />
    <input name="default" type="color3" value="0.2, 0.6, 0.4" />
  </geompropvalue>
  <UsdPrimvarReader name="reader" type="float">
    <input name="varname" type="string" value="attribute2" />
    <input name="fallback" type="float" value="0.5" />
  </UsdPrimvarReader>
  <standard_surface name="S" type="surfaceshader">
    <input name="base" type="float" nodename="reader" />
    <input name="base_color" type="color3" nodename="primvar" />
    <input name="specular" type="float" value="0" />
  </standard_surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)";
}

// Geomprop nodes: an attribute the hit has, else the
// node's default, as MaterialX and Typhoon give for a primvar the mesh
// lacks; an attribute parameter, whose node halcyon builds, reads (0, 0, 0, 1)
// where the hit lacks it, as the 'preview' renderer does
void testGeomProps(anari::Device d)
{
  clearMessages();
  const float4 value(0.5f, 0.25f, 0.75f, 1.f);
  auto m = inlineMaterial(d, geompropDocument("attribute1"));

  Pixels p = renderFloor(d, m, "attribute1", Source::NONE, value);
  check(near(p.pathtracer, float4(0.1f, 0.3f, 0.2f, 1.f)),
      "geompropvalue and UsdPrimvarReader: an attribute the floor lacks "
      "gives the node's default");
  bool present = true;
  for (Source source :
      {Source::VERTEX, Source::PRIMITIVE, Source::UNIFORM, Source::INSTANCE}) {
    p = renderFloor(d, m, "attribute1", source, value);
    present = present
        && near(p.pathtracer, float4(0.25f, 0.125f, 0.375f, 1.f));
  }
  p = renderFloor(d, m, "attribute2", Source::VERTEX, float4(0.8f));
  present = present && near(p.pathtracer, float4(0.16f, 0.48f, 0.32f, 1.f));
  check(present,
      "geompropvalue and UsdPrimvarReader: a vertex, primitive, uniform or "
      "instance attribute the floor has is its value");

  // one frame of strips one pixel wide, every other one with the attribute,
  // so that each span of the gang mixes lanes that have it and lanes that
  // don't
  auto world = stripsWorld(d, m, value);
  bool mixed = false;
  {
    Scene pt(d, world, "quality");
    const std::vector<float4> image = pt.renderImage();
    mixed = !image.empty();
    for (uint32_t x = 0; mixed && x < IMAGE_SIZE.x; x++) {
      const float4 p = image[(IMAGE_SIZE.y / 2) * IMAGE_SIZE.x + x];
      mixed = near(p,
          x % 2 == 0 ? float4(0.25f, 0.125f, 0.375f, 1.f)
                     : float4(0.1f, 0.3f, 0.2f, 1.f));
    }
  }
  anari::release(d, world);
  check(mixed,
      "geompropvalue: strips with and without the attribute in one frame "
      "each read theirs, lane by lane");
  anari::release(d, m);

  // ray-derived attributes are always there: the floor's normal (0, 0, 1)
  m = inlineMaterial(d, geompropDocument("worldNormal"));
  p = renderFloor(d, m, "attribute1", Source::NONE, value);
  check(near(p.pathtracer, float4(0.f, 0.f, 0.5f, 1.f)),
      "geompropvalue: a ray-derived attribute is always there");
  anari::release(d, m);

  // integer and boolean nodes the floor lacks give their defaults (1, true),
  // where (0, 0, 0, 1) would give 0 and false: base 1 × 1
  m = inlineMaterial(d, R"(<?xml version="1.0"?>
<materialx version="1.39">
  <geompropvalue name="i" type="integer">
    <input name="geomprop" type="string" value="attribute3" />
    <input name="default" type="integer" value="1" />
  </geompropvalue>
  <geompropvalue name="b" type="boolean">
    <input name="geomprop" type="string" value="attribute2" />
    <input name="default" type="boolean" value="true" />
  </geompropvalue>
  <convert name="fi" type="float">
    <input name="in" type="integer" nodename="i" />
  </convert>
  <convert name="fb" type="float">
    <input name="in" type="boolean" nodename="b" />
  </convert>
  <multiply name="base" type="float">
    <input name="in1" type="float" nodename="fi" />
    <input name="in2" type="float" nodename="fb" />
  </multiply>
  <standard_surface name="S" type="surfaceshader">
    <input name="base" type="float" nodename="base" />
    <input name="base_color" type="color3" value="0.8, 0.4, 0.2" />
    <input name="specular" type="float" value="0" />
  </standard_surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)");
  p = renderFloor(d, m, "attribute1", Source::NONE, value);
  check(near(p.pathtracer, float4(0.8f, 0.4f, 0.2f, 1.f)),
      "geompropvalue: integer and boolean nodes give their defaults for an "
      "attribute the floor lacks");
  anari::release(d, m);

  // a name that isn't a halcyon attribute gives the node's default, also
  // where the floor has attributes
  m = inlineMaterial(d, geompropDocument("displayColor"));
  p = renderFloor(d, m, "attribute1", Source::VERTEX, value);
  check(near(p.pathtracer, float4(0.1f, 0.3f, 0.2f, 1.f)),
      "geompropvalue: a name that isn't an attribute gives the node's "
      "default");
  anari::release(d, m);

  // an override naming an attribute: (0, 0, 0, 1) where the floor lacks it,
  // in both renderers, else its value
  m = inlineMaterial(d, lambertian("0.8, 0.4, 0.2"));
  anari::setParameter(d, m, "base_color", "attribute3");
  anari::commitParameters(d, m);
  p = renderFloor(d, m, "attribute3", Source::NONE, value);
  check(near(p.pathtracer, float4(0.f, 0.f, 0.f, 1.f))
          && near(p.standard, float4(0.f, 0.f, 0.f, 1.f)),
      "an attribute override the floor lacks reads (0, 0, 0, 1) in both "
      "renderers");
  p = renderFloor(d, m, "attribute3", Source::VERTEX, value);
  check(near(p.pathtracer, value) && near(p.standard, value),
      "an attribute override the floor has reads its value in both "
      "renderers");
  anari::release(d, m);
  check(g_errors.empty() && g_warnings.empty(),
      "geomprop documents and attribute overrides: no errors or warnings");
}

void testFile(anari::Device d)
{
  clearMessages();
  auto m = anari::newObject<anari::Material>(d, "materialx");
  const fs::path look =
      EXAMPLES / "StandardSurface" / "standard_surface_look_brass_tiled.mtlx";
  anari::setParameter(d, m, "source", look.string());
  anari::commitParameters(d, m);
  bool found = false;
  const auto names = stringList(d, m, "materialNames", &found);
  check(found
          && names
              == std::vector<std::string>{"Tiled_Brass",
                  "Greysphere_Calibration"}
          && valid(d, m) && g_errors.empty(),
      "a document file (sourceType's default): materialNames");
  check(stringList(d, m, "textureInputs", &found)
              == std::vector<std::string>{"NG_brass1/image_color/file",
                  "NG_brass1/image_roughness/file"}
          && found,
      "textureInputs: the first material's filename inputs");

  anari::setParameter(d, m, "materialName", "Greysphere_Calibration");
  anari::commitParameters(d, m);
  const auto inputs = stringList(d, m, "textureInputs", &found);
  check(found && inputs.size() == 1
          && inputs[0].find("/file") != std::string::npos && valid(d, m),
      "materialName selects another material");
  anari::release(d, m);
}

// sRGB-encoded 'v' as linear
float srgbToLinear(float v)
{
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

void testFileTexture(anari::Device d)
{
  // a flat 4 x 4 binary PPM, and a document whose base colour reads it as
  // srgb_texture
  const fs::path dir = fs::temp_directory_path()
      / ("halcyon_test_materialx_" + std::to_string(::getpid()));
  fs::create_directories(dir);
  const fs::path image = dir / "flat.ppm";
  {
    std::ofstream out(image, std::ios::binary);
    out << "P6\n4 4\n255\n";
    for (int i = 0; i < 16; i++)
      out << char(200) << char(100) << char(50);
  }
  const std::string doc = R"(<?xml version="1.0"?>
<materialx version="1.39" colorspace="lin_rec709">
  <image name="img" type="color3">
    <input name="file" type="filename" value="flat.ppm"
           colorspace="srgb_texture" />
  </image>
  <standard_surface name="S" type="surfaceshader">
    <input name="base" type="float" value="1" />
    <input name="base_color" type="color3" nodename="img" />
    <input name="specular" type="float" value="0" />
  </standard_surface>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)";
  const fs::path docPath = dir / "textured.mtlx";
  {
    std::ofstream out(docPath);
    out << doc;
  }

  clearMessages();
  auto m = anari::newObject<anari::Material>(d, "materialx");
  anari::setParameter(d, m, "source", docPath.string());
  anari::commitParameters(d, m);
  auto world = floorWorld(d, m);
  {
    Scene pt(d, world, "quality");
    const float4 expected(srgbToLinear(200.f / 255.f),
        srgbToLinear(100.f / 255.f),
        srgbToLinear(50.f / 255.f),
        1.f);
    const float4 pixel = pt.render();
    bool found = false;
    check(near(pixel, expected) && g_errors.empty() && g_warnings.empty()
            && stringList(d, m, "textureInputs", &found)
                == std::vector<std::string>{"img/file"},
        "an image file next to the document, its sRGB colours converted");

    // its texture input replaced by a file that doesn't exist: one warning
    clearMessages();
    anari::setParameter(d, m, "img/file", "missing.png");
    anari::commitParameters(d, m);
    const float4 missing = pt.render();
    anari::setParameter(d, m, "base_color", float3(0.5f)); // finalizes again
    anari::commitParameters(d, m);
    pt.render();
    anari::unsetParameter(d, m, "base_color");
    anari::commitParameters(d, m);
    pt.render();
    int warnings = 0;
    for (const auto &w : g_warnings)
      warnings += w.find("missing.png' doesn't read") != std::string::npos;
    check(near(missing, float4(0.f, 0.f, 0.f, 1.f)) && warnings == 1
            && valid(d, m),
        "a file that doesn't read warns once and gives the default value");
  }
  anari::release(d, world);
  anari::release(d, m);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// A sphere of radius 0.9 at the origin, lit from the upper left
anari::World sphereWorld(anari::Device d, anari::Material mat)
{
  const float3 centre(0.f);
  auto geom = anari::newObject<anari::Geometry>(d, "sphere");
  anari::setParameterArray1D(d, geom, "vertex.position", &centre, 1);
  anari::setParameter(d, geom, "radius", 0.9f);
  anari::commitParameters(d, geom);

  auto surface = anari::newObject<anari::Surface>(d);
  anari::setAndReleaseParameter(d, surface, "geometry", geom);
  anari::setParameter(d, surface, "material", mat);
  anari::commitParameters(d, surface);

  auto world = anari::newObject<anari::World>(d);
  anari::setParameterArray1D(d, world, "surface", &surface, 1);
  anari::release(d, surface);
  auto light = anari::newObject<anari::Light>(d, "directional");
  anari::setParameter(
      d, light, "direction", normalize(float3(1.f, -1.f, -1.f)));
  anari::setParameter(d, light, "irradiance", PI);
  anari::commitParameters(d, light);
  anari::setParameterArray1D(d, world, "light", &light, 1);
  anari::release(d, light);
  anari::commitParameters(d, world);
  return world;
}

// Every material of MaterialX's example documents through the pathtracer:
// it reads without errors or warnings, is valid but for the hair (whose
// graph doesn't compile), and renders finite colours, never
// invalidMaterialColor. With HALCYON_MATERIALX_RENDERS set to a directory,
// each image is written there as a PPM for a look.
//
// Comparing them with Typhoon (through a USD stage
// that references each .mtlx) isn't scripted here: Typhoon's side needs
// OpenUSD's colour-space names where the documents use MaterialX's
// ("srgb_texture"), which usdMtlx leaves unconverted (see AGENTS.md,
// "Materials in the pathtracer").
void testExamples(anari::Device d)
{
  const float4 MAGENTA(1.f, 0.f, 1.f, 1.f);
  std::vector<fs::path> files;
  for (const auto &entry : fs::recursive_directory_iterator(EXAMPLES)) {
    if (entry.path().extension() == ".mtlx")
      files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  const char *dumpDir = std::getenv("HALCYON_MATERIALX_RENDERS");

  auto m = anari::newObject<anari::Material>(d, "materialx");
  auto world = sphereWorld(d, m);
  int materials = 0, rendered = 0;
  bool ok = true;
  std::vector<std::string> invalid;
  {
    Scene pt(d, world, "quality");
    anari::setParameter(d, pt.frame, "size", uint2(32, 32));
    anari::commitParameters(d, pt.frame);
    for (const fs::path &file : files) {
      anari::unsetParameter(d, m, "materialName");
      anari::setParameter(d, m, "source", file.string());
      anari::commitParameters(d, m);
      bool found = false;
      const auto names = stringList(d, m, "materialNames", &found);
      if (!found) {
        // no properties: the material is invalid with its first material,
        // which only the hair's document (its only material) is
        invalid.push_back(file.filename().string());
        continue;
      }
      for (const std::string &name : names) {
        clearMessages();
        materials++;
        anari::setParameter(d, m, "materialName", name);
        anari::commitParameters(d, m);
        anari::render(d, pt.frame);
        anari::wait(d, pt.frame);
        const bool isValid = valid(d, m);
        if (!isValid) {
          invalid.push_back(name);
          continue;
        }
        auto fb = anari::map<float4>(d, pt.frame, "channel.color");
        bool finite = fb.data != nullptr;
        bool magenta = false;
        for (uint32_t i = 0; fb.data && i < fb.width * fb.height; i++) {
          const float4 p = fb.data[i];
          finite = finite && std::isfinite(p.x) && std::isfinite(p.y)
              && std::isfinite(p.z) && p.x >= 0.f && p.y >= 0.f && p.z >= 0.f;
          magenta =
              magenta || (p.x == MAGENTA.x && p.y == 0.f && p.z == MAGENTA.z);
        }
        if (dumpDir && fb.data) {
          std::ofstream out(
              fs::path(dumpDir) / (name + ".ppm"), std::ios::binary);
          out << "P6\n" << fb.width << " " << fb.height << "\n255\n";
          for (int y = int(fb.height) - 1; y >= 0; y--) {
            for (uint32_t x = 0; x < fb.width; x++) {
              const float4 p = fb.data[y * fb.width + x];
              for (float c : {p.x, p.y, p.z}) {
                const float v = std::pow(std::clamp(c, 0.f, 1.f), 1.f / 2.2f);
                out << char(int(v * 255.f + 0.5f));
              }
            }
          }
        }
        anari::unmap(d, pt.frame, "channel.color");
        const bool clean = g_errors.empty() && g_warnings.empty();
        if (!finite || magenta || !clean) {
          std::printf("    %s %s:%s%s%s\n",
              file.filename().c_str(),
              name.c_str(),
              finite ? "" : " non-finite",
              magenta ? " invalidMaterialColor" : "",
              clean ? "" : " messages");
          printMessages();
          ok = false;
        }
        rendered++;
      }
    }
  }
  anari::release(d, world);
  anari::release(d, m);
  std::printf("  %d of %d materials render\n", rendered, materials);
  check(ok && rendered == materials && rendered == 65
          && invalid == std::vector<std::string>{"simple_hair_default.mtlx"},
      "every material of MaterialX's example documents renders through the "
      "pathtracer, but the hair's");
}

void testInvalid(anari::Device d)
{
  anari::setParameter(d, d, "invalidMaterialColor", INVALID);
  anari::commitParameters(d, d);
  clearMessages();
  auto m = inlineMaterial(d, "<materialx version=\"1.39\"><broken");
  auto world = floorWorld(d, m);
  {
    Scene pt(d, world, "quality");
    Scene def(d, world, "default");
    float4 ptPixel = pt.render();
    float4 defPixel = def.render();
    check(contains(g_errors, "the inline document doesn't read") && !valid(d, m)
            && near(ptPixel, INVALID) && near(defPixel, INVALID),
        "a document that doesn't read is an ERROR; the surface shows "
        "invalidMaterialColor in both renderers");
    bool found = true;
    stringList(d, m, "materialNames", &found);
    check(!found, "no materialNames without a successful commit");

    anari::setParameter(d, d, "allowInvalidMaterials", false);
    anari::commitParameters(d, d);
    check(near(def.render(), BACKGROUND) && near(pt.render(), BACKGROUND),
        "with allowInvalidMaterials false, the surface isn't there");
    anari::setParameter(d, d, "allowInvalidMaterials", true);
    anari::commitParameters(d, d);

    // fixed by a commit: the surface sees the material valid again
    clearMessages();
    anari::setParameter(d, m, "source", lambertian("0.5, 0.5, 0.5"));
    anari::commitParameters(d, m);
    check(near(pt.render(), float4(0.5f, 0.5f, 0.5f, 1.f))
            && near(def.render(), float4(0.5f, 0.5f, 0.5f, 1.f)) && valid(d, m)
            && g_errors.empty(),
        "a commit that fixes the document makes the material valid again");

    clearMessages();
    anari::setParameter(d, m, "materialName", "Nope");
    anari::commitParameters(d, m);
    ptPixel = pt.render();
    defPixel = def.render();
    check(contains(g_errors, "no material 'Nope'") && !valid(d, m)
            && near(ptPixel, INVALID) && near(defPixel, INVALID),
        "an unknown materialName is an ERROR, and invalid");

    // the hair: its graph doesn't compile (MaterialXCpp lacks its helpers)
    clearMessages();
    anari::unsetParameter(d, m, "materialName");
    anari::setParameter(d, m, "sourceType", "documentFile");
    anari::setParameter(d,
        m,
        "source",
        (EXAMPLES / "SimpleHair" / "simple_hair_default.mtlx").string());
    anari::commitParameters(d, m);
    ptPixel = pt.render();
    bool listed = true;
    stringList(d, m, "materialNames", &listed);
    check(contains(g_errors, "doesn't compile") && !valid(d, m)
            && near(ptPixel, INVALID) && !listed,
        "a graph that doesn't compile is an ERROR, and invalid (no "
        "properties)");

    clearMessages();
    anari::setParameter(d, m, "sourceType", "documentTypo");
    anari::setParameter(d, m, "source", "/nonexistent.mtlx");
    anari::commitParameters(d, m);
    pt.render();
    check(contains(g_warnings, "unknown sourceType 'documentTypo'")
            && contains(g_errors, "'/nonexistent.mtlx' doesn't read")
            && !valid(d, m),
        "an unknown sourceType warns and reads a file; a missing file is an "
        "ERROR");
  }
  anari::release(d, world);
  anari::release(d, m);
  anari::setParameter(d, d, "invalidMaterialColor", float4(1.f, 0.f, 1.f, 1.f));
  anari::commitParameters(d, d);
}

} // namespace

int main()
{
  auto lib = anari::loadLibrary("halcyon", statusFunc);
  if (!lib) {
    std::printf("FAIL could not load the halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(lib, "default");
  testIntrospection(lib, d);
  testInline(d);
  testInvalidSamplerOverride(d, lambertian("0.8, 0.4, 0.2"), "normal");
  testInvalidSamplerOverride(
      d, openPbrLambertian("0.8, 0.4, 0.2"), "geometry_normal");
  testGeomProps(d);
  testFile(d);
  testFileTexture(d);
  testExamples(d);
  testInvalid(d);
  anari::release(d, d);
  anari::unloadLibrary(lib);
  return g_failures == 0 ? 0 : 1;
}
