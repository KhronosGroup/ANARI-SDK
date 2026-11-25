// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// MaterialX documents as MaterialXCpp graphs
// (renderer/pathtracer/MaterialXDocument.*, docs/adr/0006-materialx-
// documents.md), without the device:
//
// - MaterialX's example documents read and compile, all but the hair (whose
//   helper nodes MaterialXCpp lacks), with their shading models single native
//   nodes;
// - fileprefix and relative file names resolve against the document;
// - nodegraph/output and interfacename connections, MaterialX colour spaces
//   (mapped to OpenUSD's names and inherited from the document), Typhoon's
//   UsdPrimvarReader and UsdTransform2d renames, volume and displacement
//   shaders;
// - read errors and unknown materials are diagnostics, not exceptions;
// - overrides: values, attributes and samplers on shader inputs, files and
//   samplers on texture inputs, and what they refuse.

#include "renderer/pathtracer/ColorManagement.h"
#include "renderer/pathtracer/HalcyonTextureSystem.h"
#include "renderer/pathtracer/MaterialXDocument.h"
#include "renderer/pathtracer/PathMaterial.h"
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace halcyon::pathtracer;
using mxcpp::Value;
using mxcpp::Vec3f;
using mxcpp::Vec4f;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok)
    g_failures++;
}

const fs::path EXAMPLES =
    fs::path(HALCYON_MATERIALX_RESOURCES) / "Materials" / "Examples";

void print(const std::vector<MaterialXDiagnostic> &diagnostics)
{
  for (const auto &d : diagnostics) {
    const char *s = d.severity == MaterialXDiagnostic::Severity::ERROR ? "ERROR"
        : d.severity == MaterialXDiagnostic::Severity::WARNING ? "WARNING"
                                                               : "DEBUG";
    std::printf("    %s: %s\n", s, d.message.c_str());
  }
}

bool has(const std::vector<MaterialXDiagnostic> &diagnostics,
    MaterialXDiagnostic::Severity severity,
    const std::string &text)
{
  for (const auto &d : diagnostics) {
    if (d.severity == severity && d.message.find(text) != std::string::npos)
      return true;
  }
  return false;
}

int count(const std::vector<MaterialXDiagnostic> &diagnostics,
    MaterialXDiagnostic::Severity severity)
{
  return int(std::count_if(diagnostics.begin(),
      diagnostics.end(),
      [&](const MaterialXDiagnostic &d) { return d.severity == severity; }));
}

std::unique_ptr<CppMaterial> compile(
    const mxcpp::MaterialGraph &graph, std::string *diagnostic = nullptr)
{
  std::string d;
  auto m = CppMaterial::compile(graph, {}, &d);
  if (diagnostic)
    *diagnostic = d;
  return m;
}

MaterialXMaterial readInline(
    const std::string &xml, const std::string &material = "")
{
  return readMaterialX(MaterialXSourceType::DOCUMENT_INLINE, xml, material);
}

const mxcpp::GraphNode *node(const MaterialXMaterial &m, const std::string &key)
{
  const auto it = m.graph.nodes.find(key);
  return it == m.graph.nodes.end() ? nullptr : &it->second;
}

template <typename T>
bool param(const mxcpp::GraphNode *n, const std::string &name, const T &v)
{
  if (!n)
    return false;
  const auto it = n->parameters.find(name);
  return it != n->parameters.end() && mxcpp::ValueHolds<T>(it->second)
      && mxcpp::ValueGet<T>(it->second) == v;
}

bool near(const Vec3f &a, const Vec3f &b, float eps = 1e-5f)
{
  return std::abs(a[0] - b[0]) < eps && std::abs(a[1] - b[1]) < eps
      && std::abs(a[2] - b[2]) < eps;
}

bool connected(const mxcpp::GraphNode *n,
    const std::string &input,
    const std::string &upstream,
    const std::string &output = "out")
{
  if (!n)
    return false;
  const auto it = n->inputConnections.find(input);
  return it != n->inputConnections.end() && it->second.size() == 1
      && it->second[0].upstreamNode == upstream
      && it->second[0].upstreamOutputName == output;
}

const std::string MATERIAL_TAIL = R"(
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
</materialx>)";

// The example documents /////////////////////////////////////////////////////

void testExamples()
{
  std::vector<fs::path> files;
  for (const auto &entry : fs::recursive_directory_iterator(EXAMPLES)) {
    if (entry.path().extension() == ".mtlx")
      files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  int materials = 0, compiled = 0;
  std::vector<std::string> failed;
  bool native = true;
  bool clean = true;
  for (const fs::path &file : files) {
    const MaterialXMaterial first =
        readMaterialX(MaterialXSourceType::DOCUMENT_FILE, file.string(), "");
    if (!first.valid) {
      std::printf("  %s\n", file.filename().c_str());
      print(first.diagnostics);
      failed.push_back(file.filename().string());
      continue;
    }
    for (const std::string &name : first.materialNames) {
      materials++;
      const MaterialXMaterial m = readMaterialX(
          MaterialXSourceType::DOCUMENT_FILE, file.string(), name);
      std::string diagnostic;
      const bool ok = m.valid && compile(m.graph, &diagnostic) != nullptr;
      std::printf("  %s %s: %s, %zu nodes%s%s\n",
          file.filename().c_str(),
          name.c_str(),
          m.shaderNodeDef.c_str(),
          m.graph.nodes.size(),
          ok ? "" : ", doesn't compile: ",
          ok ? "" : diagnostic.c_str());
      if (count(m.diagnostics, MaterialXDiagnostic::Severity::ERROR)
              + count(m.diagnostics, MaterialXDiagnostic::Severity::WARNING)
          > 0) {
        print(m.diagnostics);
        clean = false;
      }
      if (!ok) {
        failed.push_back(name);
        continue;
      }
      compiled++;
      // the shading model stays one node, MaterialXCpp's own
      const auto *shader = node(m, m.shaderNode);
      native = native && shader
          && (shader->nodeTypeId == "ND_standard_surface_surfaceshader"
              || shader->nodeTypeId == "ND_open_pbr_surface_surfaceshader"
              || shader->nodeTypeId == "ND_gltf_pbr_surfaceshader"
              || shader->nodeTypeId == "ND_UsdPreviewSurface_surfaceshader"
              || shader->nodeTypeId == "ND_disney_principled");
    }
  }
  std::printf("  %d of %d materials in %zu documents compile\n",
      compiled,
      materials,
      files.size());
  check(files.size() == 50 && materials == 66 && compiled == 65
          && failed == std::vector<std::string>{"simple_hair_default"},
      "MaterialX's example documents: every material compiles but the "
      "hair's, whose helper nodes MaterialXCpp lacks");
  check(clean, "the example documents read without errors or warnings");
  check(native, "their shading models stay native MaterialXCpp nodes");

  const MaterialXMaterial brass =
      readMaterialX(MaterialXSourceType::DOCUMENT_FILE,
          (EXAMPLES / "StandardSurface" / "standard_surface_brass_tiled.mtlx")
              .string(),
          "");
  check(brass.valid && brass.graph.nodes.size() == 3
          && brass.materialName == "Tiled_Brass"
          && node(brass, "NG_brass1/image_color")
          && node(brass, "NG_brass1/image_color")->nodeTypeId
              == "ND_tiledimage_color3",
      "Tiled_Brass is its standard_surface and two native tiledimage nodes");

  // fileprefix="../../../Images/", relative to the document
  const fs::path images =
      (EXAMPLES / ".." / ".." / "Images").lexically_normal();
  const std::string color = (images / "brass_color.jpg").string();
  const auto *colorImage = node(brass, "NG_brass1/image_color");
  if (colorImage && colorImage->parameters.count("file")
      && mxcpp::ValueHolds<std::string>(colorImage->parameters.at("file")))
    std::printf("  file: %s\n",
        mxcpp::ValueGet<std::string>(colorImage->parameters.at("file"))
            .c_str());
  check(param(colorImage, "file", color) && fs::exists(color),
      "fileprefix and the document's directory resolve file names");
  check(
      connected(node(brass, "SR_brass1"), "coat_color", "NG_brass1/image_color")
          && connected(node(brass, "SR_brass1"),
              "specular_roughness",
              "NG_brass1/image_roughness"),
      "nodegraph/output connections lead to the node inside the graph");
  check(param(node(brass, "NG_brass1/image_color"),
            "colorSpace:file",
            std::string("srgb_rec709_scene"))
          && !node(brass, "NG_brass1/image_roughness")
                  ->parameters.count("colorSpace:file"),
      "an srgb_texture image converts from sRGB; one in the document's "
      "lin_rec709 (the working space) doesn't");
  std::vector<std::string> inputs;
  for (const auto &t : brass.textureInputs)
    inputs.push_back(t.first);
  check(inputs
          == std::vector<std::string>{"NG_brass1/image_color/file",
              "NG_brass1/image_roughness/file"},
      "textureInputs: the graph's filename inputs by element path");
  check(brass.shaderNodeDef == "ND_standard_surface_surfaceshader"
          && brass.shaderInputs.count("base_color")
          && brass.shaderInputs.at("base_color").type == "color3"
          && brass.shaderInputs.at("thin_walled").type == "boolean",
      "the shader's inputs, from its nodedef");
}

// Connections, colour spaces, renames ////////////////////////////////////////

void testConnections()
{
  const MaterialXMaterial m = readInline(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <nodegraph name="NG">
    <input name="tint" type="color3" value="0.2, 0.4, 0.6" />
    <multiply name="m" type="color3">
      <input name="in1" type="color3" interfacename="tint" />
      <input name="in2" type="float" value="0.5" />
    </multiply>
    <separate3 name="split" type="multioutput">
      <input name="in" type="color3" nodename="m" />
    </separate3>
    <output name="out" type="color3" nodename="m" />
    <output name="green" type="float" nodename="split" output="outg" />
  </nodegraph>
  <standard_surface name="S" type="surfaceshader">
    <input name="base_color" type="color3" nodegraph="NG" output="out" />
    <input name="specular_roughness" type="float" nodegraph="NG"
           output="green" />
  </standard_surface>)"
      + MATERIAL_TAIL);
  print(m.diagnostics);
  const auto *s = node(m, "S");
  const auto *mul = node(m, "NG/m");
  check(m.valid && m.graph.nodes.size() == 3
          && connected(s, "base_color", "NG/m")
          && connected(s, "specular_roughness", "NG/split", "outg")
          && connected(node(m, "NG/split"), "in", "NG/m") && mul
          && mul->nodeTypeId == "ND_multiply_color3FA"
          && param(mul, "in1", Vec3f(0.2f, 0.4f, 0.6f))
          && param(mul, "in2", 0.5f),
      "nodegraph outputs, multiple outputs and interfacename inputs");
  check(m.graph.terminals.at("surface").upstreamNode == "S"
          && m.graph.terminals.count("volume") == 0
          && compile(m.graph) != nullptr,
      "the surface terminal is the shader node, and the graph compiles");
}

void testColorSpaces()
{
  check(openUsdColorSpace("lin_rec709") == "lin_rec709_scene"
          && openUsdColorSpace("srgb_texture") == "srgb_rec709_scene"
          && openUsdColorSpace("acescg") == "lin_ap1_scene"
          && openUsdColorSpace("g22_rec709") == "g22_rec709_scene"
          && openUsdColorSpace("g18_rec709") == "g18_rec709_scene"
          && openUsdColorSpace("g22_ap1") == "g22_ap1_scene"
          && openUsdColorSpace("lin_adobergb") == "lin_adobergb_scene"
          && openUsdColorSpace("adobergb") == "g22_adobergb_scene"
          && openUsdColorSpace("lin_displayp3") == "lin_p3d65_scene"
          && openUsdColorSpace("srgb_displayp3") == "srgb_p3d65_scene"
          && openUsdColorSpace("none").empty()
          && openUsdColorSpace("raw").empty() && openUsdColorSpace("").empty()
          && openUsdColorSpace("srgb_rec709_scene") == "srgb_rec709_scene",
      "MaterialX colour spaces map to OpenUSD's names");
  bool known = true;
  for (const char *name : {"lin_rec709",
           "srgb_texture",
           "acescg",
           "g22_rec709",
           "g18_rec709",
           "g22_ap1",
           "lin_adobergb",
           "adobergb",
           "lin_displayp3",
           "srgb_displayp3"}) {
    Vec3f rgb(0.5f);
    known = known && convertToWorkingSpace(openUsdColorSpace(name), rgb);
  }
  check(known, "colour management knows every mapped name");

  const MaterialXMaterial m = readInline(R"(<?xml version="1.0"?>
<materialx version="1.39" colorspace="srgb_texture">
  <standard_surface name="S" type="surfaceshader">
    <input name="base_color" type="color3" value="0.5, 0.5, 0.5" />
    <input name="specular_color" type="color3" value="0.5, 0.5, 0.5"
           colorspace="lin_rec709" />
    <input name="coat_color" type="color3" value="0.5, 0.25, 1"
           colorspace="acescg" />
    <input name="sheen_color" type="color3" value="0.5, 0.5, 0.5"
           colorspace="none" />
    <input name="emission_color" type="color3" value="0.5, 0.5, 0.5"
           colorspace="my_space" />
    <input name="base" type="float" value="0.5" />
  </standard_surface>)"
      + MATERIAL_TAIL);
  const auto *s = node(m, "S");
  Vec3f srgb(0.5f), aces(0.5f, 0.25f, 1.f);
  convertToWorkingSpace("srgb_rec709_scene", srgb);
  convertToWorkingSpace("lin_ap1_scene", aces);
  const auto value = [&](const char *name) {
    return s && s->parameters.count(name)
            && mxcpp::ValueHolds<Vec3f>(s->parameters.at(name))
        ? mxcpp::ValueGet<Vec3f>(s->parameters.at(name))
        : Vec3f(-1.f);
  };
  check(near(value("base_color"), srgb) && srgb[0] < 0.25f
          && near(value("specular_color"), Vec3f(0.5f))
          && near(value("coat_color"), aces)
          && near(value("sheen_color"), Vec3f(0.5f)) && param(s, "base", 0.5f),
      "colours convert from their active colour space: the document's, or "
      "their own; none leaves them");
  check(near(value("emission_color"), Vec3f(0.5f))
          && has(m.diagnostics,
              MaterialXDiagnostic::Severity::WARNING,
              "unknown colour space 'my_space'"),
      "an unknown colour space warns and leaves the value");
}

void testUsdRenames()
{
  const MaterialXMaterial m = readInline(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <UsdPrimvarReader name="reader" type="vector2">
    <input name="varname" type="string" value="attribute1" />
    <input name="fallback" type="vector2" value="0.25, 0.5" />
  </UsdPrimvarReader>
  <UsdTransform2d name="place" type="vector2">
    <input name="in" type="vector2" nodename="reader" />
    <input name="rotation" type="float" value="30" />
    <input name="scale" type="vector2" value="2, 2" />
    <input name="translation" type="vector2" value="0.1, 0.2" />
  </UsdTransform2d>
  <UsdUVTexture name="texture" type="multioutput">
    <input name="file" type="filename" value="texture.png" />
    <input name="st" type="vector2" nodename="place" />
  </UsdUVTexture>
  <UsdPreviewSurface name="S" type="surfaceshader">
    <input name="diffuseColor" type="color3" nodename="texture"
           output="rgb" />
  </UsdPreviewSurface>)"
      + MATERIAL_TAIL);
  print(m.diagnostics);
  const auto *reader = node(m, "reader");
  const auto *place = node(m, "place");
  check(reader && reader->nodeTypeId == "ND_geompropvalue_vector2"
          && param(reader, "geomprop", std::string("attribute1"))
          && param(reader, "default", mxcpp::Vec2f(0.25f, 0.5f)) && place
          && place->nodeTypeId == "ND_place2d_vector2"
          && connected(place, "texcoord", "reader")
          && param(place, "rotate", 30.f)
          && param(place, "offset", mxcpp::Vec2f(0.1f, 0.2f))
          && param(place, "scale", mxcpp::Vec2f(2.f, 2.f)),
      "UsdPrimvarReader and UsdTransform2d as Typhoon renames them");
  std::error_code ec;
  check(param(node(m, "texture"),
            "file",
            (fs::current_path(ec) / "texture.png").string())
          && compile(m.graph) != nullptr,
      "an inline document's relative file names resolve against the working "
      "directory, and the graph compiles");
}

void testTerminals()
{
  const MaterialXMaterial volume = readInline(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <anisotropic_vdf name="vdf" type="VDF">
    <input name="absorption" type="vector3" value="0.1, 0.2, 0.3" />
    <input name="scattering" type="vector3" value="0.5, 0.5, 0.5" />
  </anisotropic_vdf>
  <volume name="V" type="volumeshader">
    <input name="vdf" type="VDF" nodename="vdf" />
  </volume>
  <volumematerial name="VM" type="material">
    <input name="volumeshader" type="volumeshader" nodename="V" />
  </volumematerial>
</materialx>)");
  print(volume.diagnostics);
  check(volume.valid && volume.materialNames == std::vector<std::string>{"VM"}
          && volume.graph.terminals.count("surface") == 0
          && volume.graph.terminals.at("volume").upstreamNode == "V"
          && volume.shaderNode.empty() && compile(volume.graph) != nullptr,
      "a volume material: a volume terminal, which compiles into a medium "
      "boundary");

  const MaterialXMaterial displaced = readInline(R"(<?xml version="1.0"?>
<materialx version="1.39">
  <standard_surface name="S" type="surfaceshader" />
  <displacement name="D" type="displacementshader">
    <input name="displacement" type="float" value="0.1" />
  </displacement>
  <surfacematerial name="M" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
    <input name="displacementshader" type="displacementshader"
           nodename="D" />
  </surfacematerial>
</materialx>)");
  check(displaced.valid
          && displaced.graph.terminals.at("displacement").upstreamNode == "D"
          && has(displaced.diagnostics,
              MaterialXDiagnostic::Severity::DEBUG,
              "doesn't displace")
          && compile(displaced.graph) != nullptr,
      "a displacement shader compiles, and is reported unused");
}

// Errors and selection ///////////////////////////////////////////////////////

void testErrors()
{
  const auto failsWith = [](const MaterialXMaterial &m,
                             const std::string &text) {
    return !m.valid
        && has(m.diagnostics, MaterialXDiagnostic::Severity::ERROR, text);
  };
  check(failsWith(readInline("<materialx version=\"1.39\"><broken"),
            "the inline document doesn't read"),
      "a parse error is an ERROR, not an exception");
  check(failsWith(readMaterialX(MaterialXSourceType::DOCUMENT_FILE,
                      "/nonexistent/material.mtlx",
                      ""),
            "'/nonexistent/material.mtlx' doesn't read"),
      "a missing file is an ERROR");
  check(failsWith(readMaterialX(MaterialXSourceType::DOCUMENT_FILE, "", ""),
            "no 'source'"),
      "no source is an ERROR");
  check(failsWith(readInline("<materialx version=\"1.39\"/>"),
            "no material with a surface or volume shader"),
      "a document without materials is an ERROR");

  const std::string two = R"(<?xml version="1.0"?>
<materialx version="1.39">
  <standard_surface name="S" type="surfaceshader" />
  <open_pbr_surface name="O" type="surfaceshader" />
  <surfacematerial name="First" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="S" />
  </surfacematerial>
  <surfacematerial name="Second" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="O" />
  </surfacematerial>
</materialx>)";
  const MaterialXMaterial first = readInline(two);
  const MaterialXMaterial second = readInline(two, "Second");
  check(first.valid && first.materialName == "First"
          && first.shaderNodeDef == "ND_standard_surface_surfaceshader"
          && first.materialNames == std::vector<std::string>{"First", "Second"}
          && second.valid
          && second.shaderNodeDef == "ND_open_pbr_surface_surfaceshader",
      "materialName selects a material; empty, the first");
  const MaterialXMaterial missing = readInline(two, "Third");
  check(failsWith(missing,
            "no material 'Third' (the document has First, "
            "Second)")
          && missing.materialNames.size() == 2,
      "an unknown materialName is an ERROR naming the materials");
}

// Overrides //////////////////////////////////////////////////////////////////

// Storage for an ISPC Sampler object: zeroed, it is an invalid sampler
// (no hook), which is all the graph needs
struct alignas(64) FakeSampler
{
  unsigned char storage[4096]{};
};

void testOverrides()
{
  const MaterialXMaterial m = readInline(R"(<?xml version="1.0"?>
<materialx version="1.39" colorspace="lin_rec709">
  <image name="image" type="color3">
    <input name="file" type="filename" value="dir/texture.png"
           colorspace="srgb_texture" />
  </image>
  <standard_surface name="S" type="surfaceshader">
    <input name="base_color" type="color3" nodename="image" />
    <input name="coat_color" type="color3" value="0.1, 0.2, 0.3" />
    <input name="specular_roughness" type="float" value="0.25" />
  </standard_surface>)"
      + MATERIAL_TAIL);
  check(m.valid && m.textureInputs.count("image/file"),
      "the document for the overrides reads");

  FakeSampler samplerA, samplerB;
  std::vector<MaterialXOverride> overrides;
  const auto value = [&](const char *name, Value v) {
    MaterialXOverride o;
    o.name = name;
    o.kind = MaterialXOverride::Kind::VALUE;
    o.value = std::move(v);
    overrides.push_back(o);
  };
  const auto string = [&](const char *name, const char *s) {
    MaterialXOverride o;
    o.name = name;
    o.kind = MaterialXOverride::Kind::STRING;
    o.string = s;
    overrides.push_back(o);
  };
  const auto sampler = [&](const char *name, const void *s) {
    MaterialXOverride o;
    o.name = name;
    o.kind = MaterialXOverride::Kind::SAMPLER;
    o.sampler = s;
    overrides.push_back(o);
  };

  // values
  value("base_color", Value(Vec3f(0.9f, 0.8f, 0.7f)));
  value("metalness", Value(0.75f));
  value("thin_walled", Value(true));
  value("specular_IOR", Value(Vec3f(1.f))); // a float input
  value("nonexistent", Value(1.f));
  std::vector<MaterialXDiagnostic> diagnostics;
  ConvertedMaterial c = applyMaterialXOverrides(m, overrides, diagnostics);
  const mxcpp::GraphNode *s = &c.graph.nodes.at("S");
  check(param(s, "base_color", Vec3f(0.9f, 0.8f, 0.7f))
          && s->inputConnections.count("base_color") == 0
          && param(s, "metalness", 0.75f) && param(s, "thin_walled", true)
          && param(s, "specular_roughness", 0.25f),
      "values of the input's type replace its value or connection");
  check(!s->parameters.count("specular_IOR")
          && has(diagnostics,
              MaterialXDiagnostic::Severity::WARNING,
              "input 'specular_IOR' is a float")
          && has(diagnostics,
              MaterialXDiagnostic::Severity::WARNING,
              "'nonexistent' is neither an input of "
              "ND_standard_surface_surfaceshader nor a texture input"),
      "a value of the wrong type or for no input warns and is ignored");

  // texture inputs
  overrides.clear();
  diagnostics.clear();
  string("image/file", "other.png");
  c = applyMaterialXOverrides(m, overrides, diagnostics);
  std::error_code ec;
  check(param(&c.graph.nodes.at("image"),
            "file",
            (fs::current_path(ec) / "other.png").string())
          && diagnostics.empty(),
      "a STRING on a texture input replaces the file, resolved like the "
      "document's");
  overrides.clear();
  sampler("image/file", &samplerA);
  value("image/file", Value(1.f));
  c = applyMaterialXOverrides(m, overrides, diagnostics);
  const mxcpp::GraphNode &image = c.graph.nodes.at("image");
  check(param(&image, "file", std::string("anari:sampler/0"))
          && !image.parameters.count("colorSpace:file")
          && c.samplers == std::vector<const void *>{&samplerA}
          && has(diagnostics,
              MaterialXDiagnostic::Severity::WARNING,
              "texture input 'image/file' takes a file name"),
      "a sampler on a texture input replaces the file with its key, without "
      "colour management; a value there warns");

  // samplers and attributes on other inputs
  overrides.clear();
  diagnostics.clear();
  sampler("coat_color", &samplerA);
  sampler("specular_roughness", &samplerB);
  string("base_color", "color");
  string("coat_IOR", "nonexistent");
  sampler("thin_walled", &samplerA);
  c = applyMaterialXOverrides(m, overrides, diagnostics);
  s = &c.graph.nodes.at("S");
  const auto upstream = [&](const char *input) -> const mxcpp::GraphNode * {
    const auto it = s->inputConnections.find(input);
    if (it == s->inputConnections.end() || it->second.size() != 1)
      return nullptr;
    const auto n = c.graph.nodes.find(it->second[0].upstreamNode);
    return n == c.graph.nodes.end() ? nullptr : &n->second;
  };
  const auto feeds =
      [&](const mxcpp::GraphNode *n) -> const mxcpp::GraphNode * {
    if (!n || !n->inputConnections.count("in"))
      return nullptr;
    const auto it =
        c.graph.nodes.find(n->inputConnections.at("in")[0].upstreamNode);
    return it == c.graph.nodes.end() ? nullptr : &it->second;
  };
  const mxcpp::GraphNode *coat = upstream("coat_color");
  const mxcpp::GraphNode *roughness = upstream("specular_roughness");
  const mxcpp::GraphNode *base = upstream("base_color");
  check(coat && coat->nodeTypeId == "ND_convert_vector4_color3" && feeds(coat)
          && feeds(coat)->nodeTypeId == SAMPLER_NODE_TYPE
          && param(feeds(coat), "sampler", 0)
          && param(feeds(coat), "default", Vec4f(0.1f, 0.2f, 0.3f, 1.f))
          && !s->parameters.count("coat_color"),
      "a sampler on a color3 input: the sampler node, its default the "
      "authored value");
  check(roughness && roughness->nodeTypeId == "ND_extract_vector4"
          && feeds(roughness) && param(feeds(roughness), "sampler", 1)
          && param(feeds(roughness), "default", Vec4f(0.25f, 0.f, 0.f, 1.f))
          && c.samplers == std::vector<const void *>{&samplerA, &samplerB},
      "a sampler on a float input: its first component");
  check(base && base->nodeTypeId == "ND_convert_vector4_color3" && feeds(base)
          && feeds(base)->nodeTypeId == "ND_geompropvalue_vector4"
          && param(feeds(base), "geomprop", std::string("color")),
      "an attribute name on an input: the attribute's geomprop node");
  check(has(diagnostics,
            MaterialXDiagnostic::Severity::WARNING,
            "'nonexistent' names no attribute")
          && has(diagnostics,
              MaterialXDiagnostic::Severity::WARNING,
              "input 'thin_walled' is a boolean, which a sampler can't set"),
      "an unknown attribute, or a sampler on a boolean, warns");
  check(CppMaterial::compile(c.graph, c.samplers) != nullptr,
      "the overridden graph compiles");
  check(c.graph.nodes.size() > m.graph.nodes.size()
          && m.graph.nodes.at("S").inputConnections.count("base_color"),
      "overrides edit a copy; the converted material stays as read");
}

} // namespace

int main()
{
  testExamples();
  testConnections();
  testColorSpaces();
  testUsdRenames();
  testTerminals();
  testErrors();
  testOverrides();
  return g_failures == 0 ? 0 : 1;
}
