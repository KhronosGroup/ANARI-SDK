// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The pathtracer's material interface (renderer/pathtracer/PathMaterial.*,
// docs/adr/0004-pathtracer-material-interface.md): ISPC kernels
// (test_path_material.ispc) shade points through it, with gangs mixing C++
// materials (MaterialXCpp graphs), native kinds (the Lambertian, matte,
// physicallyBased) and
// lanes without a material, and the results are compared with Typhoon's
// path-vertex sequence (pathIntegrator.cpp, lighting.cpp, sss.cpp,
// visibility.cpp; its BSDF sample with halcyon's weighting of a mix's delta
// samples, ClosureSampling.h) run directly in C++ with the whole shading
// context, bitwise
// for C++ lanes, within rounding for native ones; native matte against its
// own MaterialXCpp graph, which it also runs through with the renderer's
// 'nativeMaterials' false (bitwise then), and native physicallyBased bitwise
// against its graph both ways (where the C++ rounds as PathPbm.ispc does,
// else within rounding: native_rounding.h). The interface
// sends each material only the parts of the shading context its graph reads
// (PathContextGroup), and materials that read each part check that it
// arrives, and that sending every part changes nothing. Also checks the
// closure arena's lifetime and many threads shading at once.
//
// Usage: halcyon_test_path_material [--bench]
// --bench also prints what shading through the interface costs per point
// against the same work in C++, and native kinds against their graphs.

#include "Sampler_ispc.h"
#include "test_path_material_ispc.h"

#include "native_rounding.h"
#include "renderer/pathtracer/ClosureAlbedo.h"
#include "renderer/pathtracer/ClosureClassification.h"
#include "renderer/pathtracer/ClosureSampling.h"
#include "renderer/pathtracer/ColorManagement.h"
#include "renderer/pathtracer/MaterialConverter.h"
#include "renderer/pathtracer/PathMaterial.h"
// typhoon
#include <renderer/materials/MaterialXCpp/materials/adobeOpenPbr.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/spectral.h>
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace mxcpp;
using halcyon::pathtracer::CppMaterial;

namespace {

// Must match test_path_material.ispc
constexpr int IN_STRIDE = 33;
constexpr int OUT_STRIDE = 52;
constexpr int SHADOW_STRIDE = 15;

constexpr float PI = 3.14159265358979323846f;

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok)
    g_failures++;
}

// Materials //////////////////////////////////////////////////////////////////

struct TestMaterial
{
  std::string name;
  ispc::MaterialKind kind;
  std::unique_ptr<CppMaterial> cpp;
  Vec3f color{0.f};
  // The native kinds' opacity (PathShadeInput::baseOpacity)
  float opacity{1.f};
  // MATERIAL_KIND_PHYSICALLY_BASED's parameters (constants)
  std::unique_ptr<ispc::PhysicallyBasedParameters> pbm;
  // How much its sharpest GGX lobe amplifies rounding (ggxConditioning())
  float conditioning{1.f};
};

std::vector<TestMaterial> g_materials;

enum MaterialIndex
{
  M_PREVIEW_EMISSIVE,
  M_OPENPBR_COAT,
  M_PREVIEW_NOISE,
  M_OPENPBR_GLASS,
  M_OPENPBR_SUBSURFACE,
  M_OPENPBR_CUTOUT,
  // Each reads one part of the shading context (PathContextGroup)
  M_GEOMCOLOR,
  M_WORLD_POSITION,
  M_BUMP,
  M_SPLIT,
  // Native matte (with its graph): blend at opacity 0.6; a colour above 1
  // and below 0, opaque
  M_MATTE,
  M_MATTE_UNBOUNDED,
  // physicallyBased as Material.cpp converts it: native (with its graph)
  // where nativePhysicallyBasedCovers() says so. ANARI's defaults (a rough
  // metal); a rough dielectric, blend, emissive, not thin-walled; a partly
  // metallic glossy one; delta lobes (roughness 0, which caustic pruning
  // removes), all metal; a lobe sharp but not delta; colours outside [0, 1]
  M_PBM_DEFAULT,
  M_PBM_DIELECTRIC,
  M_PBM_MIXED,
  M_PBM_DELTA,
  M_PBM_DELTA_METAL,
  M_PBM_SHARP,
  M_PBM_UNBOUNDED,
  // a delta clearcoat; a rough one with normal maps of its own and the
  // base's; a sheen; a sheen and a rough clearcoat over a delta metal (what
  // pruning leaves of it: sheen and clearcoat); a delta clearcoat over a
  // delta metal (all specular)
  M_PBM_CLEARCOAT,
  M_PBM_COAT_NORMAL_MAPS,
  M_PBM_SHEEN,
  M_PBM_SHEEN_COAT,
  M_PBM_COAT_DELTA_METAL,
  // over a delta metal, a sheen alone and a rough clearcoat alone (what
  // pruning leaves: the sheen, the clearcoat)
  M_PBM_SHEEN_DELTA_METAL,
  M_PBM_COAT_OVER_DELTA_METAL,
  // transmission: rough and thin-walled; delta glass, thick; a partial one
  // under a half metal; a rough one (what pruning leaves: the reflection);
  // a delta half under a delta metal (pruning: the diffuse lobe, weighed
  // twice); and a thin film, which stays C++
  M_PBM_TRANSMISSION,
  M_PBM_GLASS,
  M_PBM_PARTIAL_TRANSMISSION,
  M_PBM_ROUGH_TRANSMISSION,
  M_PBM_DELTA_HALF_TRANSMISSION,
  M_PBM_IRIDESCENT,
  // with a normal map: rough, delta, and its sampler invalid
  M_PBM_NORMAL_MAP,
  M_PBM_NORMAL_MAP_DELTA,
  M_PBM_NORMAL_MAP_INVALID,
  M_LAMBERT,
  M_NONE,
  M_CPP_WITHOUT_GRAPH,
  NUM_MATERIALS
};

// No material on the surface
constexpr int M_NULL = -1;

// The ISPC Materials, one per g_materials entry, sending each C++ material's
// own contextMask() of the shading context
std::vector<char> g_ispcMaterials;

// Each C++ material's own contextMask()
uint32_t ownContextMask(int material)
{
  const auto &cpp = g_materials[material].cpp;
  return cpp ? cpp->contextMask() : 0u;
}

// ISPC Materials as g_ispcMaterials, sending contextMask(material) of the
// shading context instead
std::vector<char> makeIspcMaterials(
    const std::function<uint32_t(int)> &contextMask)
{
  const int size = ispc::test_sizeofMaterial();
  std::vector<char> materials(size_t(size) * NUM_MATERIALS);
  for (int i = 0; i < NUM_MATERIALS; i++) {
    const auto &m = g_materials[i];
    ispc::test_setMaterial(&materials[size_t(size) * i],
        int(m.kind),
        m.cpp.get(),
        contextMask(i),
        m.color.x,
        m.color.y,
        m.color.z,
        m.opacity);
    if (m.pbm)
      ispc::test_setPhysicallyBased(&materials[size_t(size) * i], m.pbm.get());
    ispc::test_setAttributeMask(
        &materials[size_t(size) * i], m.cpp ? m.cpp->attributeMask() : 0u);
  }
  return materials;
}

// An ISPC Sampler object, filled with Sampler.ispc's setters as the device's
// samplers fill theirs (invalid until it has an image)
struct IspcSampler
{
  alignas(64) unsigned char storage[512];

  IspcSampler()
  {
    ispc::Sampler_setDefaults(storage);
  }

  void *get()
  {
    return storage;
  }

  // A nearest, clampToEdge image2D of 'width' x 'height' 'texels' (of its
  // default inAttribute, attribute0)
  void setImage2D(const std::vector<Vec4f> &texels, int width, int height)
  {
    const int size[3] = {width, height, 1};
    const ispc::WrapMode wrap[3] = {ispc::WRAP_MODE_CLAMP_TO_EDGE,
        ispc::WRAP_MODE_CLAMP_TO_EDGE,
        ispc::WRAP_MODE_CLAMP_TO_EDGE};
    ispc::Sampler_setImage2D(storage,
        &texels[0][0],
        ispc::TEXEL_TYPE_FLOAT32,
        4,
        nullptr,
        size,
        wrap,
        false);
  }
};

// physicallyBased's normal maps: a 4 x 4 image of tangent-space normals in
// [-1, 1], with the special ones: (-1, -1, -1), which the graph's encoding
// makes 0 (and its normal map node (0, 0, 1)), (0, 0, 0) (a zero normal),
// and one pointing into the surface; and one that stays invalid
std::vector<Vec4f> g_normalTexels;
IspcSampler g_normalMap;
IspcSampler g_invalidNormalMap;

void makeNormalMaps()
{
  std::mt19937 rng(77);
  std::uniform_real_distribution<float> u(-1.f, 1.f);
  for (int i = 0; i < 13; i++) {
    const Vec3f n =
        Vec3f(u(rng) * 0.8f, u(rng) * 0.8f, 0.6f + 0.4f * std::abs(u(rng)))
            .normalized();
    g_normalTexels.push_back(Vec4f(n[0], n[1], n[2], 1.f));
  }
  g_normalTexels.push_back(Vec4f(-1.f, -1.f, -1.f, 1.f));
  g_normalTexels.push_back(Vec4f(0.f, 0.f, 0.f, 1.f));
  g_normalTexels.push_back(Vec4f(0.3f, -0.2f, -0.9f, 1.f));
  g_normalMap.setImage2D(g_normalTexels, 4, 4);
}

MaterialGraph terminalOnly(
    const std::string &nodeTypeId, std::map<std::string, Value> parameters)
{
  MaterialGraph network;
  GraphNode node;
  node.nodeTypeId = nodeTypeId;
  node.parameters = std::move(parameters);
  network.nodes["/Material/Surface"] = node;
  network.terminals["surface"] = {"/Material/Surface", "out"};
  return network;
}

std::unique_ptr<CppMaterial> compile(
    const MaterialGraph &graph, std::vector<const void *> samplers = {})
{
  std::string diagnostic;
  auto m = CppMaterial::compile(graph, std::move(samplers), &diagnostic);
  if (!m)
    std::printf("compile failed: %s\n", diagnostic.c_str());
  return m;
}

bool makeMaterials()
{
  g_materials.resize(NUM_MATERIALS);
  auto cpp = [](int i, const char *name, const MaterialGraph &graph) {
    g_materials[i].name = name;
    g_materials[i].kind = ispc::MATERIAL_KIND_CPP;
    g_materials[i].cpp = compile(graph);
  };

  cpp(M_PREVIEW_EMISSIVE,
      "UsdPreviewSurface, emissive",
      terminalOnly("UsdPreviewSurface",
          {{"diffuseColor", Value(Vec3f(0.8f, 0.4f, 0.2f))},
              {"emissiveColor", Value(Vec3f(0.5f, 0.25f, 0.125f))},
              {"roughness", Value(0.4f)},
              {"metallic", Value(0.25f)},
              {"clearcoat", Value(0.5f)}}));
  cpp(M_OPENPBR_COAT,
      "OpenPBR, coat + fuzz",
      terminalOnly("ND_open_pbr_surface_surfaceshader",
          {{"base_color", Value(Vec3f(0.2f, 0.4f, 0.6f))},
              {"base_metalness", Value(0.3f)},
              {"specular_roughness", Value(0.35f)},
              {"coat_weight", Value(0.6f)},
              {"coat_roughness", Value(0.1f)},
              {"fuzz_weight", Value(0.3f)}}));

  MaterialGraph noisy = terminalOnly("UsdPreviewSurface",
      {{"roughness", Value(0.6f)}, {"metallic", Value(0.0f)}});
  GraphNode noise;
  noise.nodeTypeId = "ND_noise3d_color3";
  noisy.nodes["/Material/Noise"] = noise;
  noisy.nodes["/Material/Surface"].inputConnections["diffuseColor"] = {
      {"/Material/Noise", "out"}};
  cpp(M_PREVIEW_NOISE, "UsdPreviewSurface, noise3d colour", noisy);

  cpp(M_OPENPBR_GLASS,
      "OpenPBR, dispersive glass with a medium",
      terminalOnly("ND_open_pbr_surface_surfaceshader",
          {{"transmission_weight", Value(1.f)},
              {"transmission_color", Value(Vec3f(0.5f, 0.8f, 0.9f))},
              {"transmission_depth", Value(1.f)},
              {"transmission_dispersion_scale", Value(1.f)},
              {"specular_roughness", Value(0.2f)}}));
  cpp(M_OPENPBR_SUBSURFACE,
      "OpenPBR, subsurface",
      terminalOnly("ND_open_pbr_surface_surfaceshader",
          {{"subsurface_weight", Value(1.f)},
              {"subsurface_color", Value(Vec3f(0.9f, 0.5f, 0.4f))},
              {"subsurface_radius", Value(0.5f)},
              {"subsurface_scatter_anisotropy", Value(0.3f)}}));
  cpp(M_OPENPBR_CUTOUT,
      "OpenPBR, geometry_opacity 0.5",
      terminalOnly("ND_open_pbr_surface_surfaceshader",
          {{"geometry_opacity", Value(0.5f)},
              {"base_color", Value(Vec3f(0.3f, 0.6f, 0.3f))}}));

  // A UsdPreviewSurface whose diffuse colour is the 'out' of a node
  const auto diffuseFrom = [](const std::string &type,
                               std::map<std::string, Value> parameters) {
    MaterialGraph g = terminalOnly("UsdPreviewSurface",
        {{"roughness", Value(0.5f)}, {"metallic", Value(0.f)}});
    GraphNode n;
    n.nodeTypeId = type;
    n.parameters = std::move(parameters);
    g.nodes["/Material/Node"] = n;
    g.nodes["/Material/Surface"].inputConnections["diffuseColor"] = {
        {"/Material/Node", "out"}};
    return g;
  };
  cpp(M_GEOMCOLOR,
      "geomcolor (display colour)",
      diffuseFrom("ND_geomcolor_color3", {}));
  cpp(M_WORLD_POSITION,
      "world position (transforms)",
      diffuseFrom(
          "ND_position_vector3", {{"space", Value(std::string("world"))}}));
  cpp(M_SPLIT,
      "splitlr (screen-space derivatives)",
      diffuseFrom("ND_splitlr_color3",
          {{"valuel", Value(Vec3f(0.9f, 0.1f, 0.1f))},
              {"valuer", Value(Vec3f(0.1f, 0.1f, 0.9f))},
              {"center", Value(0.5f)}}));
  // a bump of the object-space x coordinate: finite differences over the
  // position's derivatives, with steps from the screen-space ones
  MaterialGraph bump = terminalOnly("ND_gltf_pbr_surfaceshader",
      {{"base_color", Value(Vec3f(0.6f))}, {"roughness", Value(0.3f)}});
  GraphNode position;
  position.nodeTypeId = "ND_position_vector3";
  position.parameters["space"] = Value(std::string("object"));
  bump.nodes["/Material/Position"] = position;
  GraphNode height;
  height.nodeTypeId = "ND_extract_vector3";
  height.parameters["index"] = Value(0);
  height.inputConnections["in"] = {{"/Material/Position", "out"}};
  bump.nodes["/Material/Height"] = height;
  GraphNode bumpNode;
  bumpNode.nodeTypeId = "ND_bump_vector3";
  bumpNode.parameters["scale"] = Value(0.5f);
  bumpNode.inputConnections["height"] = {{"/Material/Height", "out"}};
  bump.nodes["/Material/Bump"] = bumpNode;
  bump.nodes["/Material/Surface"].inputConnections["normal"] = {
      {"/Material/Bump", "out"}};
  cpp(M_BUMP, "bump (surface and screen-space derivatives)", bump);

  // matte as Material.cpp converts it: native, with its graph
  const auto matte = [](int i,
                         const char *name,
                         const Vec3f &color,
                         float opacity,
                         halcyon::pathtracer::AlphaMode alphaMode) {
    halcyon::pathtracer::MatteParameters params;
    params.color = halcyon::pathtracer::MaterialParameter::constant(color);
    params.opacity = halcyon::pathtracer::MaterialParameter::constant(opacity);
    params.alphaMode = alphaMode;
    g_materials[i].name = name;
    g_materials[i].kind = ispc::MATERIAL_KIND_MATTE;
    g_materials[i].cpp = compile(halcyon::pathtracer::matteGraph(params).graph);
    g_materials[i].color = color;
    // what the integrator resolves: opacity after alphaMode
    g_materials[i].opacity =
        alphaMode == halcyon::pathtracer::AlphaMode::BLEND ? opacity : 1.f;
  };
  matte(M_MATTE,
      "native matte, blend",
      Vec3f(0.7f, 0.5f, 0.3f),
      0.6f,
      halcyon::pathtracer::AlphaMode::BLEND);
  matte(M_MATTE_UNBOUNDED,
      "native matte, colour outside [0, 1]",
      Vec3f(1.5f, -0.25f, 0.4f),
      1.f,
      halcyon::pathtracer::AlphaMode::OPAQUE);

  // physicallyBased as Material.cpp converts it: native where it covers
  // the parameters, with its graph
  using halcyon::pathtracer::MaterialParameter;
  using halcyon::pathtracer::PhysicallyBasedParameters;
  const auto pbm =
      [](int i, const char *name, PhysicallyBasedParameters params) {
        const bool native =
            halcyon::pathtracer::nativePhysicallyBasedCovers(params);
        g_materials[i].name = name;
        g_materials[i].kind = native ? ispc::MATERIAL_KIND_PHYSICALLY_BASED
                                     : ispc::MATERIAL_KIND_CPP;
        auto converted = halcyon::pathtracer::physicallyBasedGraph(params);
        g_materials[i].cpp =
            compile(converted.graph, std::move(converted.samplers));
        const Vec4f &c = params.baseColor.value;
        g_materials[i].color = Vec3f(c[0], c[1], c[2]);
        // glTF's roughness squared is MaterialXCpp's alpha
        const auto conditioning = [](float roughness) {
          return native_rounding::ggxConditioning(roughness * roughness);
        };
        g_materials[i].conditioning =
            std::max(conditioning(params.roughness.value[0]),
                params.clearcoat.value[0] > 0.f
                    ? conditioning(params.clearcoatRoughness.value[0])
                    : 1.f);
        // what the integrator resolves: alpha (colour alpha times opacity)
        // after alphaMode
        g_materials[i].opacity =
            params.alphaMode == halcyon::pathtracer::AlphaMode::BLEND
            ? c[3] * params.opacity.value[0]
            : 1.f;
        if (!native)
          return;
        auto p = std::make_unique<ispc::PhysicallyBasedParameters>();
        const auto set = [](ispc::MaterialParameter &out,
                             const MaterialParameter &in) {
          for (int k = 0; k < 4; k++)
            out.value.v[k] = in.value[k];
          out.attribute = ispc::ATTRIBUTE_NONE;
          out.sampler = nullptr;
        };
        set(p->metallic, params.metallic);
        set(p->roughness, params.roughness);
        set(p->specular, params.specular);
        set(p->specularColor, params.specularColor);
        set(p->ior, params.ior);
        set(p->emissive, params.emissive);
        set(p->clearcoat, params.clearcoat);
        set(p->clearcoatRoughness, params.clearcoatRoughness);
        set(p->sheenColor, params.sheenColor);
        set(p->sheenRoughness, params.sheenRoughness);
        set(p->transmission, params.transmission);
        set(p->thickness, params.thickness);
        p->normal = static_cast<const ispc::Sampler *>(params.normal);
        p->clearcoatNormal =
            static_cast<const ispc::Sampler *>(params.clearcoatNormal);
        g_materials[i].pbm = std::move(p);
      };
  const auto with =
      [](std::initializer_list<
             std::pair<MaterialParameter PhysicallyBasedParameters::*, Vec3f>>
              values,
          halcyon::pathtracer::AlphaMode alphaMode =
              halcyon::pathtracer::AlphaMode::OPAQUE,
          float opacity = 1.f) {
        PhysicallyBasedParameters p;
        for (const auto &[member, v] : values)
          p.*member = MaterialParameter::constant(v);
        p.opacity = MaterialParameter::constant(opacity);
        p.alphaMode = alphaMode;
        return p;
      };
  using P = PhysicallyBasedParameters;
  pbm(M_PBM_DEFAULT,
      "native physicallyBased, defaults",
      with({{&P::baseColor, Vec3f(0.8f, 0.6f, 0.4f)}}));
  pbm(M_PBM_DIELECTRIC,
      "native physicallyBased, dielectric",
      with({{&P::baseColor, Vec3f(0.2f, 0.5f, 0.8f)},
               {&P::metallic, Vec3f(0.f)},
               {&P::roughness, Vec3f(0.4f)},
               {&P::specular, Vec3f(1.f)},
               {&P::emissive, Vec3f(0.1f, 0.2f, 0.3f)},
               {&P::thickness, Vec3f(1.f)}},
          halcyon::pathtracer::AlphaMode::BLEND,
          0.7f));
  pbm(M_PBM_MIXED,
      "native physicallyBased, half metal",
      with({{&P::baseColor, Vec3f(0.9f, 0.7f, 0.3f)},
          {&P::metallic, Vec3f(0.5f)},
          {&P::roughness, Vec3f(0.15f)},
          {&P::specular, Vec3f(0.5f)},
          {&P::specularColor, Vec3f(1.f, 0.8f, 0.6f)},
          {&P::ior, Vec3f(2.f)}}));
  pbm(M_PBM_DELTA,
      "native physicallyBased, delta lobes",
      with({{&P::baseColor, Vec3f(0.3f, 0.6f, 0.4f)},
          {&P::metallic, Vec3f(0.3f)},
          {&P::roughness, Vec3f(0.f)},
          {&P::specular, Vec3f(1.f)}}));
  pbm(M_PBM_DELTA_METAL,
      "native physicallyBased, delta metal",
      with({{&P::baseColor, Vec3f(0.95f, 0.64f, 0.54f)},
          {&P::roughness, Vec3f(0.01f)}}));
  pbm(M_PBM_SHARP,
      "native physicallyBased, sharp lobes",
      with({{&P::baseColor, Vec3f(0.5f)},
          {&P::metallic, Vec3f(0.6f)},
          {&P::roughness, Vec3f(0.035f)},
          {&P::specular, Vec3f(0.8f)}}));
  pbm(M_PBM_UNBOUNDED,
      "native physicallyBased, colours outside [0, 1]",
      with({{&P::baseColor, Vec3f(1.5f, -0.2f, 0.5f)},
          {&P::metallic, Vec3f(0.7f)},
          {&P::roughness, Vec3f(0.6f)},
          {&P::specular, Vec3f(1.2f)},
          {&P::specularColor, Vec3f(1.3f, -0.1f, 0.9f)},
          {&P::emissive, Vec3f(2.f, 0.f, 0.5f)}}));
  pbm(M_PBM_CLEARCOAT,
      "native physicallyBased, delta clearcoat",
      with({{&P::baseColor, Vec3f(0.6f, 0.2f, 0.2f)},
          {&P::metallic, Vec3f(0.f)},
          {&P::roughness, Vec3f(0.5f)},
          {&P::clearcoat, Vec3f(1.f)}}));

  makeNormalMaps();
  {
    P p = with({{&P::baseColor, Vec3f(0.3f, 0.4f, 0.8f)},
        {&P::metallic, Vec3f(0.4f)},
        {&P::roughness, Vec3f(0.45f)},
        {&P::specular, Vec3f(1.f)},
        {&P::clearcoat, Vec3f(0.7f)},
        {&P::clearcoatRoughness, Vec3f(0.3f)}});
    p.normal = g_normalMap.get();
    p.clearcoatNormal = g_normalMap.get();
    pbm(M_PBM_COAT_NORMAL_MAPS,
        "native physicallyBased, rough clearcoat, normal maps",
        p);
  }
  pbm(M_PBM_SHEEN,
      "native physicallyBased, sheen",
      with({{&P::baseColor, Vec3f(0.5f, 0.2f, 0.3f)},
          {&P::metallic, Vec3f(0.2f)},
          {&P::roughness, Vec3f(0.4f)},
          {&P::sheenColor, Vec3f(0.8f, 0.6f, 0.7f)},
          {&P::sheenRoughness, Vec3f(0.5f)}}));
  pbm(M_PBM_SHEEN_COAT,
      "native physicallyBased, sheen and clearcoat over a delta metal",
      with({{&P::baseColor, Vec3f(0.9f, 0.8f, 0.5f)},
               {&P::metallic, Vec3f(1.f)},
               {&P::roughness, Vec3f(0.f)},
               {&P::sheenColor, Vec3f(0.3f, 0.3f, 0.4f)},
               {&P::sheenRoughness, Vec3f(0.2f)},
               {&P::clearcoat, Vec3f(1.f)},
               {&P::clearcoatRoughness, Vec3f(0.2f)}},
          halcyon::pathtracer::AlphaMode::BLEND,
          0.8f));
  pbm(M_PBM_COAT_DELTA_METAL,
      "native physicallyBased, delta clearcoat over a delta metal",
      with({{&P::baseColor, Vec3f(0.7f, 0.7f, 0.75f)},
          {&P::metallic, Vec3f(1.f)},
          {&P::roughness, Vec3f(0.01f)},
          {&P::clearcoat, Vec3f(0.5f)}}));
  pbm(M_PBM_SHEEN_DELTA_METAL,
      "native physicallyBased, sheen over a delta metal",
      with({{&P::baseColor, Vec3f(0.8f, 0.8f, 0.85f)},
          {&P::metallic, Vec3f(1.f)},
          {&P::roughness, Vec3f(0.f)},
          {&P::sheenColor, Vec3f(0.2f, 0.5f, 0.4f)},
          {&P::sheenRoughness, Vec3f(0.7f)}}));
  pbm(M_PBM_COAT_OVER_DELTA_METAL,
      "native physicallyBased, rough clearcoat over a delta metal",
      with({{&P::baseColor, Vec3f(0.6f, 0.65f, 0.7f)},
          {&P::metallic, Vec3f(1.f)},
          {&P::roughness, Vec3f(0.f)},
          {&P::clearcoat, Vec3f(0.9f)},
          {&P::clearcoatRoughness, Vec3f(0.5f)}}));
  pbm(M_PBM_GLASS,
      "native physicallyBased, delta glass",
      with({{&P::baseColor, Vec3f(0.95f, 0.98f, 1.f)},
          {&P::metallic, Vec3f(0.f)},
          {&P::roughness, Vec3f(0.f)},
          {&P::specular, Vec3f(1.f)},
          {&P::transmission, Vec3f(1.f)},
          {&P::thickness, Vec3f(1.f)}}));
  pbm(M_PBM_PARTIAL_TRANSMISSION,
      "native physicallyBased, partial transmission under a half metal",
      with({{&P::baseColor, Vec3f(0.6f, 0.8f, 0.4f)},
          {&P::metallic, Vec3f(0.3f)},
          {&P::roughness, Vec3f(0.3f)},
          {&P::specular, Vec3f(0.7f)},
          {&P::ior, Vec3f(1.33f)},
          {&P::transmission, Vec3f(0.6f)},
          {&P::thickness, Vec3f(0.5f)}}));
  pbm(M_PBM_ROUGH_TRANSMISSION,
      "native physicallyBased, rough transmission",
      with({{&P::baseColor, Vec3f(0.9f, 0.7f, 0.6f)},
          {&P::metallic, Vec3f(0.f)},
          {&P::roughness, Vec3f(0.6f)},
          {&P::specular, Vec3f(1.f)},
          {&P::ior, Vec3f(2.2f)},
          {&P::transmission, Vec3f(1.f)},
          {&P::thickness, Vec3f(2.f)}}));
  pbm(M_PBM_DELTA_HALF_TRANSMISSION,
      "native physicallyBased, delta half transmission under a delta metal",
      with({{&P::baseColor, Vec3f(0.5f, 0.5f, 0.9f)},
          {&P::metallic, Vec3f(0.4f)},
          {&P::roughness, Vec3f(0.f)},
          {&P::specular, Vec3f(1.f)},
          {&P::transmission, Vec3f(0.5f)}}));
  pbm(M_PBM_IRIDESCENT,
      "physicallyBased with a thin film (C++)",
      with({{&P::baseColor, Vec3f(0.7f)},
          {&P::roughness, Vec3f(0.3f)},
          {&P::iridescence, Vec3f(1.f)},
          {&P::iridescenceThickness, Vec3f(400.f)}}));
  pbm(M_PBM_TRANSMISSION,
      "native physicallyBased, transmission",
      with({{&P::baseColor, Vec3f(0.9f)},
          {&P::metallic, Vec3f(0.f)},
          {&P::roughness, Vec3f(0.2f)},
          {&P::transmission, Vec3f(1.f)}}));
  {
    P p = with({{&P::baseColor, Vec3f(0.4f, 0.7f, 0.5f)},
        {&P::metallic, Vec3f(0.f)},
        {&P::roughness, Vec3f(0.3f)},
        {&P::specular, Vec3f(1.f)}});
    p.normal = g_normalMap.get();
    pbm(M_PBM_NORMAL_MAP, "native physicallyBased, normal map", p);
    p = with({{&P::baseColor, Vec3f(0.8f, 0.5f, 0.3f)},
        {&P::metallic, Vec3f(0.5f)},
        {&P::roughness, Vec3f(0.f)},
        {&P::specular, Vec3f(1.f)}});
    p.normal = g_normalMap.get();
    pbm(M_PBM_NORMAL_MAP_DELTA,
        "native physicallyBased, normal map, delta lobes",
        p);
    p = with({{&P::baseColor, Vec3f(0.7f)}, {&P::roughness, Vec3f(0.5f)}});
    p.normal = g_invalidNormalMap.get();
    pbm(M_PBM_NORMAL_MAP_INVALID,
        "native physicallyBased, invalid normal map",
        p);
  }

  g_materials[M_LAMBERT] = {"native Lambertian",
      ispc::MATERIAL_KIND_LAMBERT,
      {},
      Vec3f(0.6f, 0.5f, 0.4f)};
  g_materials[M_NONE] = {"MATERIAL_KIND_NONE", ispc::MATERIAL_KIND_NONE, {}};
  g_materials[M_CPP_WITHOUT_GRAPH] = {
      "C++ kind without a material", ispc::MATERIAL_KIND_CPP, {}};

  bool compiled = true;
  for (int i = 0; i < NUM_MATERIALS; i++) {
    if (g_materials[i].kind != ispc::MATERIAL_KIND_LAMBERT
        && g_materials[i].kind != ispc::MATERIAL_KIND_NONE
        && i != M_CPP_WITHOUT_GRAPH)
      compiled = compiled && g_materials[i].cpp;
  }

  g_ispcMaterials = makeIspcMaterials(&ownContextMask);
  return compiled;
}

ispc::Material *ispcMaterials(std::vector<char> &materials = g_ispcMaterials)
{
  return reinterpret_cast<ispc::Material *>(materials.data());
}

// Points //////////////////////////////////////////////////////////////////////

Vec3f randomDirection(std::mt19937 &rng)
{
  std::normal_distribution<float> d;
  Vec3f v;
  do {
    v = Vec3f(d(rng), d(rng), d(rng));
  } while (v.length2() < 1e-6f);
  return v.normalized();
}

void orthonormalBasis(const Vec3f &n, Vec3f &t, Vec3f &b)
{
  const float sign = std::copysign(1.f, n.z);
  const float a = -1.f / (sign + n.z);
  const float c = n.x * n.y * a;
  t = Vec3f(1.f + sign * n.x * n.x * a, sign * c, -sign * n.x);
  b = Vec3f(c, sign + n.y * n.y * a, -n.y);
}

struct PointOptions
{
  // Fixed material, or -2 for a random one of all (M_NULL included)
  int material{-2};
  float pruneFraction{0.25f};
  // Points asking for the closure's albedo (PATH_SHADE_ALBEDO)
  float albedoFraction{0.25f};
  // Points whose path is already spectral (hero wavelength 550 nm)
  float spectralFraction{0.25f};
};

std::vector<float> makePoints(int n, const PointOptions &o, uint32_t seed)
{
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> u01(0.f, 1.f);
  std::vector<float> in(size_t(n) * IN_STRIDE, 0.f);
  for (int i = 0; i < n; i++) {
    float *p = &in[size_t(i) * IN_STRIDE];
    int m = o.material;
    if (m == -2)
      m = int(u01(rng) * (NUM_MATERIALS + 1)) % (NUM_MATERIALS + 1) - 1;
    p[0] = float(m);
    for (int k = 0; k < 3; k++)
      p[1 + k] = 4.f * u01(rng) - 2.f;
    const Vec3f Ng = randomDirection(rng);
    const Vec3f Nsrf = (Ng + 0.2f * randomDirection(rng)).normalized();
    Vec3f T, B;
    orthonormalBasis(Nsrf, T, B);
    // normal maps fall back to dPdu where the tangent vanishes
    if ((m == M_PBM_NORMAL_MAP || m == M_PBM_NORMAL_MAP_DELTA)
        && u01(rng) < 0.1f)
      T = Vec3f(0.f);
    // Mostly front-facing views, some from behind
    Vec3f wo = randomDirection(rng);
    if (wo.dot(Ng) < 0.f && u01(rng) < 0.8f)
      wo = -wo;
    Vec3f wl = randomDirection(rng);
    if (wl.dot(Ng) * wo.dot(Ng) < 0.f && u01(rng) < 0.7f)
      wl = -wl;
    for (int k = 0; k < 3; k++) {
      p[4 + k] = Ng[k];
      p[7 + k] = Nsrf[k];
      p[10 + k] = T[k];
      p[13 + k] = B[k];
      p[16 + k] = wo[k];
      p[28 + k] = wl[k];
    }
    p[19] = u01(rng);
    p[20] = u01(rng);
    p[21] = u01(rng) < o.spectralFraction ? 550.f : 0.f;
    uint32_t flags = 0;
    if (Ng.dot(wo) > 0.f)
      flags |= ispc::PATH_SHADE_FRONT_FACING;
    if (u01(rng) < o.pruneFraction)
      flags |= ispc::PATH_SHADE_PRUNE_CAUSTICS;
    if (u01(rng) < o.albedoFraction)
      flags |= ispc::PATH_SHADE_ALBEDO;
    p[22] = float(flags);
    for (int k = 23; k <= 27; k++)
      p[k] = u01(rng);
    p[31] = u01(rng);
    p[32] = u01(rng);
  }
  return in;
}

Vec3f v3(const float *p, int o)
{
  return Vec3f(p[o], p[o + 1], p[o + 2]);
}

void put3(float *q, int o, const Vec3f &v)
{
  q[o] = v.x;
  q[o + 1] = v.y;
  q[o + 2] = v.z;
}

void putInt(float *q, int o, int v)
{
  std::memcpy(&q[o], &v, sizeof(int));
}

int getInt(const float *q, int o)
{
  int v;
  std::memcpy(&v, &q[o], sizeof(int));
  return v;
}

// A point's attribute0 (test_path_material.ispc's loadInput() has the same)
Vec4f attributeOf(const float *p)
{
  return Vec4f(p[19], p[20], 0.5f, 1.f);
}

// The geomprop lookup of a point's material (PathMaterial.cpp's
// lookupGeomProp()): its attribute0, the only attribute a test graph reads
struct PointGeomProps
{
  const CppMaterial *material;
  Vec4f attribute0;
};

Value lookupPointGeomProp(const void *userData, int handle)
{
  const auto &l = *static_cast<const PointGeomProps *>(userData);
  const auto &bindings = l.material->geomProps();
  if (handle < 0 || size_t(handle) >= bindings.size()
      || bindings[handle].attribute != ispc::ATTRIBUTE_0)
    return {};
  const Vec4f &a = l.attribute0;
  switch (bindings[handle].type) {
  case halcyon::pathtracer::GeomPropType::FLOAT:
    return Value(a[0]);
  case halcyon::pathtracer::GeomPropType::VECTOR2:
    return Value(Vec2f(a[0], a[1]));
  case halcyon::pathtracer::GeomPropType::VECTOR3:
    return Value(Vec3f(a[0], a[1], a[2]));
  case halcyon::pathtracer::GeomPropType::VECTOR4:
    return Value(a);
  default:
    return {};
  }
}

// The whole shading context of a point, as test_path_material.ispc's
// loadInput() makes it up (Typhoon's visibility-only evaluations leave the
// screen-space derivatives zero: 'screenSpaceDerivatives' false), with the
// geomprop lookup and texture system of 'material' through 'props' (which
// must outlive it)
ShadingContext contextOf(const float *p,
    bool screenSpaceDerivatives = true,
    const CppMaterial *material = nullptr,
    PointGeomProps *props = nullptr)
{
  ShadingContext ctx;
  if (material && props) {
    *props = {material, attributeOf(p)};
    ctx.geomPropLookup = &lookupPointGeomProp;
    ctx.geomPropUserData = props;
    ctx.textureSystem = &material->textures();
  }
  ctx.position = v3(p, 1);
  ctx.normal = v3(p, 7);
  ctx.tangent = v3(p, 10);
  ctx.bitangent = v3(p, 13);
  ctx.texcoord = Vec2f(p[19], p[20]);
  ctx.displayColor = Vec3f(p[19], p[20], 0.25f);
  ctx.displayOpacity = 0.75f;
  ctx.faceId = 3;
  ctx.baryU = p[20];
  ctx.baryV = p[19];
  ctx.dPdu = ctx.tangent + ctx.bitangent;
  ctx.dPdv = ctx.bitangent - ctx.tangent;
  ctx.dPositiondu = ctx.tangent * 2.f;
  ctx.dPositiondv = ctx.bitangent * 3.f;
  if (screenSpaceDerivatives) {
    ctx.dPdx = ctx.tangent * 0.01f;
    ctx.dPdy = ctx.bitangent * 0.02f;
    ctx.dPositiondx = ctx.tangent * 0.03f;
    ctx.dPositiondy = ctx.bitangent * 0.04f;
    ctx.dudx = 0.3f;
    ctx.dvdx = 0.05f;
    ctx.dudy = 0.2f;
    ctx.dvdy = 0.25f;
  }
  // test_path_material.ispc's objectToWorldOfPoints() and its inverse, as
  // Imath's row-vector matrices
  ctx.objectToWorldMatrix = Mat4f(0.f,
      2.f,
      0.f,
      0.f,
      -2.f,
      0.f,
      0.f,
      0.f,
      0.f,
      0.f,
      2.f,
      0.f,
      1.f,
      -2.f,
      3.f,
      1.f);
  ctx.worldToObjectMatrix = Mat4f(0.f,
      -0.5f,
      0.f,
      0.f,
      0.5f,
      0.f,
      0.f,
      0.f,
      0.f,
      0.f,
      0.5f,
      0.f,
      1.f,
      0.5f,
      -1.5f,
      1.f);
  ctx.hasObjectToWorldTransform = true;
  ctx.hasWorldToObjectTransform = true;
  // the renderer's services, as Typhoon's _BuildShadingContext() sets them
  // from its working space (MaterialXCpp's default luminance coefficients
  // differ from OpenUSD's in the last bits)
  ctx.luminanceCoefficients = halcyon::pathtracer::luminanceCoefficients();
  ctx.colorTransform = &halcyon::pathtracer::colorTransform;
  return ctx;
}

// Expected results ///////////////////////////////////////////////////////////

// What one point's row should hold. 'approximate' rows (native kinds) are
// compared within a tolerance: ISPC's transcendentals differ from libm's, and
// ISPC fuses multiply-adds.
struct Expected
{
  std::vector<float> row = std::vector<float>(OUT_STRIDE, 0.f);
  bool kept{false};
  bool approximate{false};
};

// Typhoon's path vertex for a C++ material, written out from
// pathIntegrator.cpp (graph evaluation to the BSDF sample), lighting.cpp
// (the NEE evaluation), sss.cpp (_TraceSubsurface()'s entry) and
// volumeTransport.cpp (_UpdatePathMedium()'s medium)
void expectCpp(const CppMaterial *material,
    const float *p,
    bool useAdobeOpenPBR,
    bool subsurfaceExit,
    Expected &e,
    bool alwaysAlbedo = false)
{
  float *q = e.row.data();
  const uint32_t shadeFlags = uint32_t(p[22]);
  const bool frontFacing = shadeFlags & ispc::PATH_SHADE_FRONT_FACING;
  const Vec3f Ng = v3(p, 4), Nsrf = v3(p, 7), wo = v3(p, 16);
  const Vec3f NgOut = frontFacing ? Ng : -Ng;
  const Vec3f NsrfOut = frontFacing ? Nsrf : -Nsrf;
  put3(q, 5, NsrfOut);
  if (!material && !subsurfaceExit)
    return;

  PointGeomProps props;
  const ShadingContext ctx = contextOf(p, true, material, &props);
  SurfaceClosure closure;
  if (subsurfaceExit) {
    closure.baseColor = Vec3f(1.f);
    closure.roughness = 1.f;
    closure.specular = 0.f;
    closure.specularColor = Vec3f(0.f);
    closure.opacity = 1.f;
    closure.presence = 1.f;
  } else {
    EvalOptions options;
    options.useAdobeOpenPBR = useAdobeOpenPBR;
    closure = material->graph().Evaluate(ctx, options);
  }

  const Vec3f NsExt = ResolveGraphNormal(closure, ctx);
  ValidateLeafNormals(&closure.bsdfTree, NsExt);
  const Vec3f NsOut = frontFacing ? NsExt : -NsExt;
  Bsdf::detail::PrepareShadingNormals(&closure.bsdfTree, NsOut, NgOut, wo);
  put3(q, 5, NsOut);

  if (closure.presence < 1.f) {
    const float presence = std::clamp(closure.presence, 0.f, 1.f);
    if (presence <= 0.f || p[23] >= presence) {
      putInt(q, 1, ispc::PATH_MATERIAL_PRESENCE_SKIPPED);
      return;
    }
    closure.presence = 1.f;
  }

  using halcyon::pathtracer::hasCoupledThickDielectric;
  using halcyon::pathtracer::isReflectionOnlyClosure;
  using halcyon::pathtracer::isVolumeOnlyBoundary;

  SurfaceClosure pruned;
  const bool volumeOnly = isVolumeOnlyBoundary(closure);
  const SurfaceClosure *bsdf = volumeOnly ? nullptr : &closure;
  if (shadeFlags & ispc::PATH_SHADE_PRUNE_CAUSTICS) {
    pruned = Bsdf::PruneCausticClassLobes(closure);
    if (closure.HasBsdfTree() && !pruned.HasBsdfTree())
      bsdf = nullptr;
    else if (bsdf)
      bsdf = &pruned;
  }

  uint32_t flags = ispc::PATH_MATERIAL_HAS_CLOSURE;
  if (bsdf)
    flags |= ispc::PATH_MATERIAL_HAS_BSDF;
  if (volumeOnly)
    flags |= ispc::PATH_MATERIAL_VOLUME_ONLY_BOUNDARY;
  if (closure.thinWalled)
    flags |= ispc::PATH_MATERIAL_THIN_WALLED;
  if (isReflectionOnlyClosure(closure))
    flags |= ispc::PATH_MATERIAL_REFLECTION_ONLY;
  if (bsdf && isReflectionOnlyClosure(*bsdf))
    flags |= ispc::PATH_MATERIAL_BSDF_REFLECTION_ONLY;
  if (bsdf && hasCoupledThickDielectric(*bsdf))
    flags |= ispc::PATH_MATERIAL_COUPLED_THICK_DIELECTRIC;
  if (closure.hasInteriorMedium)
    flags |= ispc::PATH_MATERIAL_HAS_INTERIOR_MEDIUM;
  if (bsdf && bsdf->HasSubsurfaceScattering())
    flags |= ispc::PATH_MATERIAL_HAS_SUBSURFACE;
  if ((shadeFlags & ispc::PATH_SHADE_ALBEDO) || alwaysAlbedo) {
    const auto albedo = halcyon::pathtracer::closureAlbedo(closure);
    put3(q, 49, albedo.albedo);
    if (albedo.specularOnly && (shadeFlags & ispc::PATH_SHADE_ALBEDO))
      flags |= ispc::PATH_MATERIAL_SPECULAR_ONLY;
  }

  float hero = p[21];
  if (bsdf && bsdf->HasDispersion() && hero == 0.f) {
    hero = Spectral::SampleHeroWavelength(p[24]);
    q[9] = Spectral::HeroWavelengthPdf();
    flags |= ispc::PATH_MATERIAL_HERO_STARTED;
  }
  q[8] = hero;

  SurfaceInteraction si;
  si.normalShdWldOut = NsOut;
  si.normalSrfWldOut = NsrfOut;
  si.normalGeomWldOut = NgOut;
  si.omegaOutWld = wo;
  si.heroWavelengthNm = hero;
  si.frontFacing = frontFacing;
  AdobeOpenPbrPreparedSurface adobe;
  if (bsdf)
    adobe = PrepareAdobeOpenPbrSurface(*bsdf, si);

  Bsdf::BsdfSample s;
  if (bsdf) {
    s = adobe.valid
        ? SamplePreparedAdobeOpenPbrSurface(adobe, p[25], p[26], p[27])
        // Typhoon's SampleSurface() with a mix's delta samples weighed as
        // its evaluation weighs them (closure_sampling checks it against
        // Typhoon's)
        : halcyon::pathtracer::sampleSurface(*bsdf, si, p[25], p[26], p[27]);
    put3(q, 10, s.omegaInWld);
    put3(q,
        13,
        s.isSpecular || s.isSubsurface ? s.bsdfValue : s.bsdfValueCosine);
    q[16] = s.pdfSolidAngle;
    putInt(q,
        17,
        (s.isSubsurface || s.pdfSolidAngle > 0.f ? ispc::PATH_SAMPLE_VALID : 0)
            | (s.isSpecular ? ispc::PATH_SAMPLE_SPECULAR : 0)
            | (s.isTransmission ? ispc::PATH_SAMPLE_TRANSMISSION : 0)
            | (s.isSubsurface ? ispc::PATH_SAMPLE_SUBSURFACE : 0)
            | (s.isDiffuseLike ? ispc::PATH_SAMPLE_DIFFUSE_LIKE : 0));
    q[18] = s.eta;

    // NEE towards the light direction
    const Vec3f wl = v3(p, 28);
    const AdobeOpenPbrEvalPdfResult a = adobe.valid
        ? EvalPdfPreparedAdobeOpenPbrSurface(adobe, wl)
        : TryEvalPdfAdobeOpenPbrSurface(*bsdf, si, wl);
    Vec3f f =
        a.evaluated ? a.valueCosine : Bsdf::EvalSurfaceCosine(*bsdf, si, wl);
    for (int k = 0; k < 3; k++)
      f[k] = std::isfinite(f[k]) ? f[k] : 0.f;
    put3(q, 19, f);
    q[22] = a.evaluated ? a.pdfSolidAngle : Bsdf::PdfSurface(*bsdf, si, wl);

    if (s.isSubsurface) {
      Vec3f dir = s.omegaInWld;
      bool valid = true;
      if (!s.hasSubsurfaceEntryDirection) {
        valid = Bsdf::SampleSubsurfaceEntry(
            *bsdf, s.normalShdLobeWldOut, NgOut, wo, p[31], p[32], dir);
      }
      valid =
          valid && s.normalShdLobeWldOut.dot(dir) < 0.f && NgOut.dot(dir) < 0.f;
      putInt(q, 23, valid);
      if (valid)
        put3(q, 24, dir);
      put3(q, 27, bsdf->subsurfaceColor);
      put3(q, 30, bsdf->subsurfaceRadius * bsdf->subsurfaceRadiusScale);
      q[33] = std::clamp(bsdf->subsurfaceAnisotropy, -0.99f, 0.99f);
      const MediumProperties &pre = bsdf->precomputedSubsurfaceMedium;
      if (bsdf->hasPrecomputedSubsurfaceMedium && !pre.IsVacuum()) {
        putInt(q, 34, 1);
        put3(q, 35, pre.absorption);
        put3(q, 38, pre.scattering);
        q[33] = std::clamp(pre.anisotropy, -0.99f, 0.99f);
      }
    }
  } else {
    q[18] = 1.f;
  }

  if (closure.hasInteriorMedium) {
    put3(q, 41, closure.interiorMedium.absorption);
    put3(q, 44, closure.interiorMedium.scattering);
    q[47] = closure.interiorMedium.anisotropy;
    putInt(q, 48, int(closure.interiorMedium.transportModel));
  }

  put3(q, 2, closure.emissiveColor);
  putInt(q, 1, int(flags));
  e.kept = true;
}

// The native Lambertian, as PathMaterial.isph defines it
void expectLambert(const Vec3f &albedo, const float *p, Expected &e)
{
  float *q = e.row.data();
  e.approximate = true;
  const bool frontFacing = uint32_t(p[22]) & ispc::PATH_SHADE_FRONT_FACING;
  const Vec3f n = frontFacing ? v3(p, 7) : -v3(p, 7);
  const Vec3f Ng = frontFacing ? v3(p, 4) : -v3(p, 4);
  putInt(q,
      1,
      ispc::PATH_MATERIAL_HAS_CLOSURE | ispc::PATH_MATERIAL_HAS_BSDF
          | ispc::PATH_MATERIAL_REFLECTION_ONLY
          | ispc::PATH_MATERIAL_BSDF_REFLECTION_ONLY);
  put3(q, 5, n);
  q[8] = p[21];
  put3(q, 49, albedo);

  const float u1 = p[25], u2 = p[26];
  const float r = std::sqrt(u1), phi = 2.f * PI * u2;
  const float cosTheta = std::sqrt(std::max(0.f, 1.f - u1));
  Vec3f t, b;
  orthonormalBasis(n, t, b);
  const Vec3f wi =
      (t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * cosTheta)
          .normalized();
  put3(q, 10, wi);
  q[18] = 1.f;
  if (cosTheta > 0.f && wi.dot(Ng) > 0.f) {
    q[16] = cosTheta / PI;
    put3(q, 13, albedo * q[16]);
    putInt(q, 17, ispc::PATH_SAMPLE_VALID | ispc::PATH_SAMPLE_DIFFUSE_LIKE);
  }

  const Vec3f wl = v3(p, 28);
  const float c = wl.dot(n);
  if (c > 0.f && wl.dot(Ng) > 0.f) {
    q[22] = c / PI;
    put3(q, 19, albedo * q[22]);
  }
}

// 'nativeMaterials' as the renderer's: false shades native matte through
// its graph
Expected expectVertex(
    const float *p, bool useAdobeOpenPBR, bool nativeMaterials = true)
{
  Expected e;
  float *q = e.row.data();
  putInt(q, 0, -1);
  q[8] = p[21];
  q[18] = 1.f;
  const bool frontFacing = uint32_t(p[22]) & ispc::PATH_SHADE_FRONT_FACING;
  put3(q, 5, frontFacing ? v3(p, 7) : -v3(p, 7));

  const int m = int(p[0]);
  const ispc::MaterialKind kind =
      m < 0 ? ispc::MATERIAL_KIND_NONE : g_materials[m].kind;
  const bool nativeKind = kind == ispc::MATERIAL_KIND_MATTE
      || kind == ispc::MATERIAL_KIND_PHYSICALLY_BASED;
  if (kind == ispc::MATERIAL_KIND_CPP || (nativeKind && !nativeMaterials))
    expectCpp(g_materials[m].cpp.get(), p, useAdobeOpenPBR, false, e);
  else if (nativeKind) {
    // its graph's results, within rounding, and no closure in C++'s arena;
    // native kinds always report their albedo
    expectCpp(g_materials[m].cpp.get(), p, useAdobeOpenPBR, false, e, true);
    e.kept = false;
    // physicallyBased rounds as its graph does (PathPbm.ispc)
    e.approximate = kind == ispc::MATERIAL_KIND_MATTE;
  } else if (kind == ispc::MATERIAL_KIND_LAMBERT)
    expectLambert(g_materials[m].color, p, e);
  return e;
}

// Typhoon's visibility-only evaluation at a shadow-ray blocker
std::vector<float> expectShadow(const float *p, bool useAdobeOpenPBR)
{
  std::vector<float> q(SHADOW_STRIDE, 0.f);
  const int m = int(p[0]);
  if (m < 0)
    return q;
  if (g_materials[m].kind == ispc::MATERIAL_KIND_LAMBERT) {
    putInt(q.data(), 0, ispc::PATH_MATERIAL_HAS_CLOSURE);
    q[1] = q[2] = 1.f;
    return q;
  }
  // native kinds report what their graphs do
  const CppMaterial *material = g_materials[m].cpp.get();
  if ((g_materials[m].kind != ispc::MATERIAL_KIND_CPP
          && g_materials[m].kind != ispc::MATERIAL_KIND_MATTE
          && g_materials[m].kind != ispc::MATERIAL_KIND_PHYSICALLY_BASED)
      || !material)
    return q;

  EvalOptions options;
  options.useAdobeOpenPBR = useAdobeOpenPBR;
  options.visibilityOnly = true;
  PointGeomProps props;
  const SurfaceClosure c = material->graph().Evaluate(
      contextOf(p, false, material, &props), options);
  int flags = ispc::PATH_MATERIAL_HAS_CLOSURE;
  if (halcyon::pathtracer::isVolumeOnlyBoundary(c))
    flags |= ispc::PATH_MATERIAL_VOLUME_ONLY_BOUNDARY;
  if (c.thinWalled)
    flags |= ispc::PATH_MATERIAL_THIN_WALLED;
  if (c.hasInteriorMedium)
    flags |= ispc::PATH_MATERIAL_HAS_INTERIOR_MEDIUM;
  if (c.hasInteriorMedium || halcyon::pathtracer::isVolumeOnlyBoundary(c)) {
    put3(q.data(), 8, c.interiorMedium.absorption);
    put3(q.data(), 11, c.interiorMedium.scattering);
    q[14] = c.interiorMedium.anisotropy;
  }
  putInt(q.data(), 0, flags);
  q[1] = c.opacity;
  q[2] = c.presence;
  q[3] = c.transmission;
  const float t = std::clamp(c.transmission, 0.f, 1.f);
  if (t > 0.f) {
    const Vec3f dirShadow = -v3(p, 16);
    q[4] = std::clamp(t
            * Bsdf::StraightShadowDielectricTransmission(
                c, dirShadow.dot(v3(p, 4))),
        0.f,
        1.f);
  }
  put3(q.data(), 5, c.transmissionColor);
  return q;
}

// Comparisons ////////////////////////////////////////////////////////////////

constexpr float UNWRITTEN = -7.f;

std::vector<float> runVertex(const std::vector<float> &in,
    int n,
    bool adobe,
    std::vector<char> &materials = g_ispcMaterials,
    bool nativeMaterials = true)
{
  std::vector<float> out(size_t(n) * OUT_STRIDE, UNWRITTEN);
  ispc::kernel_vertex(adobe,
      nativeMaterials,
      ispcMaterials(materials),
      in.data(),
      out.data(),
      n);
  return out;
}

std::vector<float> runShadow(const std::vector<float> &in,
    int n,
    bool adobe,
    std::vector<char> &materials = g_ispcMaterials,
    bool nativeMaterials = true)
{
  std::vector<float> out(size_t(n) * SHADOW_STRIDE, UNWRITTEN);
  ispc::kernel_shadow(adobe,
      nativeMaterials,
      ispcMaterials(materials),
      in.data(),
      out.data(),
      n);
  return out;
}

bool sameBits(float a, float b)
{
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

using native_rounding::BITWISE;
using native_rounding::ROUNDING;

bool withinRounding(float a, float b, float conditioning = 1.f)
{
  return (std::isnan(a) && std::isnan(b))
      || std::abs(a - b)
      <= ROUNDING * conditioning * std::max(1.f, std::abs(b));
}

// Whether material m's rows, shaded natively, may round apart from its
// graph's: native physicallyBased's where the C++ doesn't round as
// PathPbm.ispc does (native_rounding.h). Its values are then compared within
// rounding times the material's conditioning, and a few of its points
// (FEW_POINTS of them; at most 3 of 1024 seen with -march=native) may
// differ by more: a sample on the other side of a threshold takes another
// branch. Their values must still be within native_rounding::OUTLIER times
// the tolerance (about 1e-3 relative seen). Every other row stays as before (C++ lanes and graphs bitwise,
// native matte within rounding), as the C++ runs the same code both ways.
constexpr double FEW_POINTS = 0.01;

bool roundsApart(int m, bool nativeMaterials)
{
  return !BITWISE && nativeMaterials && m >= 0
      && g_materials[m].kind == ispc::MATERIAL_KIND_PHYSICALLY_BASED;
}

// Values of material m's rows: bitwise, or within rounding where they may
// round apart
bool matchesNative(float a, float b, int m, bool nativeMaterials)
{
  return roundsApart(m, nativeMaterials)
      ? withinRounding(a, b, g_materials[m].conditioning)
      : sameBits(a, b);
}

// Compares every point's row with expectVertex(); the handle only by sign
bool vertexMatches(const std::vector<float> &in,
    const std::vector<float> &out,
    int n,
    bool adobe,
    std::string &where,
    bool nativeMaterials = true)
{
  const int intElements[] = {1, 17, 23, 34, 48};
  int apart = 0, differ = 0;
  for (int i = 0; i < n; i++) {
    const float *p = &in[size_t(i) * IN_STRIDE];
    const float *q = &out[size_t(i) * OUT_STRIDE];
    const int m = int(p[0]);
    const Expected e = expectVertex(p, adobe, nativeMaterials);
    std::string at;
    if ((getInt(q, 0) >= 0) != e.kept)
      at = "point " + std::to_string(i) + " handle";
    for (int k = 1; k < OUT_STRIDE && at.empty(); k++) {
      const bool isInt =
          std::find(std::begin(intElements), std::end(intElements), k)
          != std::end(intElements);
      const float a = q[k], b = e.row[k];
      const bool ok = isInt ? sameBits(a, b)
          : e.approximate   ? withinRounding(a, b)
                            : matchesNative(a, b, m, nativeMaterials);
      if (!ok && !isInt && !e.approximate && roundsApart(m, nativeMaterials)
          && !withinRounding(a,
              b,
              g_materials[m].conditioning * native_rounding::OUTLIER)) {
        where = "point " + std::to_string(i) + " (material "
            + std::to_string(m) + ") element " + std::to_string(k) + ": "
            + std::to_string(a) + " vs " + std::to_string(b)
            + ", beyond OUTLIER times the tolerance";
        return false;
      }
      if (!ok) {
        at = "point " + std::to_string(i) + " (material " + std::to_string(m)
            + ") element " + std::to_string(k) + ": " + std::to_string(a)
            + " vs " + std::to_string(b);
      }
    }
    const bool mayDiffer = roundsApart(m, nativeMaterials);
    apart += mayDiffer;
    if (at.empty())
      continue;
    if (!mayDiffer) {
      where = at;
      return false;
    }
    if (differ++ == 0)
      where = at;
  }
  if (differ)
    where = std::to_string(differ) + " of " + std::to_string(apart)
        + " points differ, first " + where;
  return differ <= FEW_POINTS * apart;
}

int countFlag(const std::vector<float> &in,
    const std::vector<float> &out,
    int n,
    int material,
    int element,
    int flag)
{
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (int(in[size_t(i) * IN_STRIDE]) == material)
      count += (getInt(&out[size_t(i) * OUT_STRIDE], element) & flag) != 0;
  }
  return count;
}

// Checks /////////////////////////////////////////////////////////////////////

void checkVertexAgainstTyphoon()
{
  const int gang = ispc::test_programCount();
  for (int m = 0; m < NUM_MATERIALS; m++) {
    PointOptions o;
    o.material = m;
    const int n = 1024;
    const auto in = makePoints(n, o, 100 + m);
    const auto out = runVertex(in, n, false);
    std::string where;
    const bool ok = vertexMatches(in, out, n, false, where);
    check(ok,
        "a vertex through the interface matches Typhoon's sequence ("
            + g_materials[m].name + ")" + (where.empty() ? "" : ": " + where));
  }

  // Every material and no material mixed within gangs, a partial last gang;
  // also with native kinds sent through their graphs
  for (bool adobe : {false, true}) {
    for (bool native : {true, false}) {
      const int n = 4096 - gang / 2 - 1;
      const auto in = makePoints(n, PointOptions{}, 7);
      const auto out = runVertex(in, n, adobe, g_ispcMaterials, native);
      std::string where;
      const bool ok = vertexMatches(in, out, n, adobe, where, native);
      check(ok,
          std::string("mixed kinds in a gang match")
              + (adobe ? " (useAdobeOpenPBR)" : "")
              + (native ? "" : " (nativeMaterials false)")
              + (where.empty() ? "" : ": " + where));
    }
  }
}

// Native matte against its MaterialXCpp graph on the same points and random
// numbers: within rounding natively, bitwise through the graph with
// nativeMaterials false (the renderer parameter that validates one against
// the other)
void checkNativeMatte()
{
  for (int m : {M_MATTE, M_MATTE_UNBOUNDED}) {
    for (bool native : {true, false}) {
      PointOptions o;
      o.material = m;
      const int n = 4096;
      const auto in = makePoints(n, o, 300 + m);
      const auto out = runVertex(in, n, false, g_ispcMaterials, native);
      std::string where;
      const bool ok = vertexMatches(in, out, n, false, where, native);
      check(ok,
          g_materials[m].name
              + (native ? ": native shading matches its graph within rounding"
                        : ": nativeMaterials false shades its graph exactly")
              + (where.empty() ? "" : ": " + where));
    }
  }

  // and both ways hand the integrator the same discrete facts at every point
  PointOptions o;
  o.material = M_MATTE;
  const int n = 4096;
  const auto in = makePoints(n, o, 333);
  const auto native = runVertex(in, n, false, g_ispcMaterials, true);
  const auto graph = runVertex(in, n, false, g_ispcMaterials, false);
  int sameFlags = 0, valid = 0, skipped = 0;
  for (int i = 0; i < n; i++) {
    const float *a = &native[size_t(i) * OUT_STRIDE];
    const float *b = &graph[size_t(i) * OUT_STRIDE];
    sameFlags += getInt(a, 1) == getInt(b, 1) && getInt(a, 17) == getInt(b, 17);
    valid += (getInt(a, 17) & ispc::PATH_SAMPLE_VALID) != 0;
    skipped += (getInt(a, 1) & ispc::PATH_MATERIAL_PRESENCE_SKIPPED) != 0;
  }
  check(sameFlags == n && valid > n / 4 && skipped > n / 4,
      "native matte samples and skips by presence where its graph does ("
          + std::to_string(valid) + " samples, " + std::to_string(skipped)
          + " skipped of " + std::to_string(n) + ")");
}

// Native physicallyBased against its MaterialXCpp graph on the same points
// and random numbers, bitwise both ways where PathPbm.ispc rounds as the
// C++ does (HALCYON_NATIVE_BITWISE); and the rule that routes a material to
// it
void checkNativePhysicallyBased()
{
  const int materials[] = {M_PBM_DEFAULT,
      M_PBM_DIELECTRIC,
      M_PBM_MIXED,
      M_PBM_DELTA,
      M_PBM_DELTA_METAL,
      M_PBM_SHARP,
      M_PBM_UNBOUNDED,
      M_PBM_CLEARCOAT,
      M_PBM_COAT_NORMAL_MAPS,
      M_PBM_SHEEN,
      M_PBM_SHEEN_COAT,
      M_PBM_COAT_DELTA_METAL,
      M_PBM_SHEEN_DELTA_METAL,
      M_PBM_COAT_OVER_DELTA_METAL,
      M_PBM_TRANSMISSION,
      M_PBM_GLASS,
      M_PBM_PARTIAL_TRANSMISSION,
      M_PBM_ROUGH_TRANSMISSION,
      M_PBM_DELTA_HALF_TRANSMISSION,
      M_PBM_NORMAL_MAP,
      M_PBM_NORMAL_MAP_DELTA,
      M_PBM_NORMAL_MAP_INVALID};
  for (int m : materials) {
    check(g_materials[m].kind == ispc::MATERIAL_KIND_PHYSICALLY_BASED,
        g_materials[m].name + " is native");
    for (bool native : {true, false}) {
      PointOptions o;
      o.material = m;
      // half the points with caustic pruning, which changes delta closures
      o.pruneFraction = 0.5f;
      const int n = 4096;
      const auto in = makePoints(n, o, 400 + m);
      const auto out = runVertex(in, n, false, g_ispcMaterials, native);
      std::string where;
      const bool ok = vertexMatches(in, out, n, false, where, native);
      check(ok,
          g_materials[m].name
              + (native ? (BITWISE
                                  ? ": native shading matches its graph bitwise"
                                  : ": native shading matches its graph "
                                    "within rounding")
                        : ": nativeMaterials false shades its graph exactly")
              + (where.empty() ? "" : ": " + where));
    }
  }
  check(g_materials[M_PBM_IRIDESCENT].kind == ispc::MATERIAL_KIND_CPP,
      "physicallyBased with a thin film stays C++");

  // the normal map perturbs the shading normal at most points (not where
  // its texel is one the graph falls back from)
  {
    PointOptions o;
    o.material = M_PBM_NORMAL_MAP;
    const int n = 1024;
    const auto in = makePoints(n, o, 450);
    const auto out = runVertex(in, n, false);
    int perturbed = 0;
    for (int i = 0; i < n; i++) {
      const float *p = &in[size_t(i) * IN_STRIDE];
      const float *q = &out[size_t(i) * OUT_STRIDE];
      const bool front = uint32_t(p[22]) & ispc::PATH_SHADE_FRONT_FACING;
      const Vec3f smooth = front ? v3(p, 7) : -v3(p, 7);
      perturbed += (smooth - v3(q, 5)).length() > 1e-3f;
    }
    check(perturbed > n / 2,
        "the normal map perturbs the shading normal ("
            + std::to_string(perturbed) + " of " + std::to_string(n)
            + " points)");
  }

  // what each kind of sample the lobes give turns up
  PointOptions o;
  o.material = -2;
  const int n = 8192;
  std::vector<float> in;
  for (int m : materials) {
    o.material = m;
    const auto points = makePoints(n / int(std::size(materials)), o, 500 + m);
    in.insert(in.end(), points.begin(), points.end());
  }
  const int points = int(in.size() / IN_STRIDE);
  const auto out = runVertex(in, points, false);
  int specular = 0, glossy = 0, diffuse = 0, skipped = 0, emissive = 0,
      thin = 0, noBsdf = 0, specularOnly = 0;
  for (int i = 0; i < points; i++) {
    const float *q = &out[size_t(i) * OUT_STRIDE];
    const int flags = getInt(q, 1), sampleFlags = getInt(q, 17);
    const bool valid = sampleFlags & ispc::PATH_SAMPLE_VALID;
    specular += valid && (sampleFlags & ispc::PATH_SAMPLE_SPECULAR);
    diffuse += valid && (sampleFlags & ispc::PATH_SAMPLE_DIFFUSE_LIKE);
    glossy += valid
        && !(sampleFlags
            & (ispc::PATH_SAMPLE_SPECULAR | ispc::PATH_SAMPLE_DIFFUSE_LIKE));
    skipped += (flags & ispc::PATH_MATERIAL_PRESENCE_SKIPPED) != 0;
    emissive += q[2] > 0.f;
    thin += (flags & ispc::PATH_MATERIAL_THIN_WALLED) != 0;
    noBsdf += (flags & ispc::PATH_MATERIAL_HAS_CLOSURE)
        && !(flags & ispc::PATH_MATERIAL_HAS_BSDF);
    specularOnly += (flags & ispc::PATH_MATERIAL_SPECULAR_ONLY) != 0;
  }
  check(specular > 0 && glossy > 0 && diffuse > 0 && skipped > 0 && emissive > 0
          && thin > 0 && noBsdf > 0 && specularOnly > 0,
      "native physicallyBased covers delta (" + std::to_string(specular)
          + "), glossy (" + std::to_string(glossy) + ") and diffuse ("
          + std::to_string(diffuse) + ") samples, presence ("
          + std::to_string(skipped) + " skipped), emission, thin walls, "
          + "pruned closures without a BSDF (" + std::to_string(noBsdf)
          + ") and specular-only albedo");

  // nativePhysicallyBasedCovers(): any parameter source, except those that
  // shape the closure, which must be constants of the covered values
  using halcyon::pathtracer::MaterialParameter;
  using halcyon::pathtracer::nativePhysicallyBasedCovers;
  using P = halcyon::pathtracer::PhysicallyBasedParameters;
  int sampler = 0;
  const auto fromSampler = [&](const MaterialParameter &p) {
    return MaterialParameter::fromSampler(&sampler, p.value);
  };
  const auto covers = [&](const std::function<void(P &)> &change) {
    P p;
    change(p);
    return nativePhysicallyBasedCovers(p);
  };
  const bool admits = covers([](P &) {}) && covers([&](P &p) {
    p.metallic = fromSampler(p.metallic);
    p.roughness =
        MaterialParameter::fromAttribute("attribute0", p.roughness.value);
    p.baseColor = fromSampler(p.baseColor);
    p.specular = fromSampler(p.specular);
    p.specularColor = fromSampler(p.specularColor);
    p.ior = fromSampler(p.ior);
    p.emissive = fromSampler(p.emissive);
    p.opacity = fromSampler(p.opacity);
    p.transmission = fromSampler(p.transmission);
    p.thickness = fromSampler(p.thickness);
    p.clearcoat = fromSampler(p.clearcoat);
    p.clearcoatRoughness = fromSampler(p.clearcoatRoughness);
    p.sheenColor = fromSampler(p.sheenColor);
    p.sheenRoughness = fromSampler(p.sheenRoughness);
    p.normal = &sampler;
    p.clearcoatNormal = &sampler;
    p.occlusion = &sampler;
  }) && covers([](P &p) {
    p.transmission = MaterialParameter::constant(1.f);
  }) && covers([](P &p) {
    p.iridescence = MaterialParameter::constant(1.f); // thickness 0
  }) && covers([&](P &p) {
    p.iridescence = MaterialParameter::constant(-1.f);
    p.iridescenceThickness = fromSampler(p.iridescenceThickness);
  }) && covers([&](P &p) {
    p.iridescence = fromSampler(p.iridescence);
    p.iridescenceThickness = MaterialParameter::constant(0.f);
  });
  const bool refuses = !covers([](P &p) {
    p.iridescence = MaterialParameter::constant(1.f);
    p.iridescenceThickness = MaterialParameter::constant(400.f);
  }) && !covers([&](P &p) {
    p.iridescence = fromSampler(p.iridescence);
    p.iridescenceThickness = MaterialParameter::constant(400.f);
  }) && !covers([&](P &p) {
    p.iridescence = MaterialParameter::constant(0.5f);
    p.iridescenceThickness = fromSampler(p.iridescenceThickness);
  });
  check(admits && refuses,
      "nativePhysicallyBasedCovers() admits any source for every parameter, "
      "and refuses a thin film (iridescence and its thickness above 1e-7, or "
      "varying)");
}

// The materials do what they were chosen for, so the comparisons above cover
// each part of the interface
void checkCoverage()
{
  const int n = 4096;
  const auto in = makePoints(n, PointOptions{}, 11);
  const auto out = runVertex(in, n, false);

  auto count = [&](int m, int element, int flag) {
    return countFlag(in, out, n, m, element, flag);
  };
  check(count(M_OPENPBR_CUTOUT, 1, ispc::PATH_MATERIAL_PRESENCE_SKIPPED) > 0
          && count(M_OPENPBR_CUTOUT, 1, ispc::PATH_MATERIAL_HAS_CLOSURE) > 0,
      "presence passes some hits and skips others");
  check(count(M_OPENPBR_GLASS, 1, ispc::PATH_MATERIAL_HAS_INTERIOR_MEDIUM) > 0,
      "glass has an interior medium");
  check(count(M_OPENPBR_GLASS, 1, ispc::PATH_MATERIAL_HERO_STARTED) > 0,
      "dispersive glass starts the hero wavelength");
  check(count(M_OPENPBR_SUBSURFACE, 17, ispc::PATH_SAMPLE_SUBSURFACE) > 0
          && count(M_OPENPBR_SUBSURFACE, 23, 1) > 0,
      "subsurface samples enter the walk");
  check(count(M_OPENPBR_GLASS, 17, ispc::PATH_SAMPLE_SPECULAR) > 0
          || count(M_OPENPBR_GLASS, 17, ispc::PATH_SAMPLE_TRANSMISSION) > 0,
      "glass samples transmit");
  // (about half of its points sample: its share of the points is
  // 1 / (NUM_MATERIALS + 1))
  check(count(M_LAMBERT, 17, ispc::PATH_SAMPLE_VALID)
          > n / (4 * (NUM_MATERIALS + 1)),
      "the native Lambertian samples");

  bool heroKept = true, emissive = false;
  for (int i = 0; i < n; i++) {
    const float *p = &in[size_t(i) * IN_STRIDE];
    const float *q = &out[size_t(i) * OUT_STRIDE];
    if (p[21] > 0.f)
      heroKept = heroKept && !(getInt(q, 1) & ispc::PATH_MATERIAL_HERO_STARTED)
          && q[8] == p[21];
    if (int(p[0]) == M_PREVIEW_EMISSIVE && getInt(q, 0) >= 0)
      emissive = emissive || q[2] > 0.f;
  }
  check(heroKept, "a spectral path keeps its hero wavelength");
  check(emissive, "emission is returned");
}

void checkShadow()
{
  for (bool adobe : {false, true}) {
    const int n = 2048;
    const auto in = makePoints(n, PointOptions{}, 13);
    const std::vector<float> out = runShadow(in, n, adobe);
    bool same = true, glassTransmits = false;
    for (int i = 0; i < n && same; i++) {
      const auto e = expectShadow(&in[size_t(i) * IN_STRIDE], adobe);
      const float *q = &out[size_t(i) * SHADOW_STRIDE];
      const int m = int(in[size_t(i) * IN_STRIDE]);
      for (int k = 0; k < SHADOW_STRIDE; k++) {
        if (!matchesNative(q[k], e[k], m, true)) {
          std::printf("  point %d element %d: %g vs %g\n", i, k, q[k], e[k]);
          same = false;
        }
      }
      if (int(in[size_t(i) * IN_STRIDE]) == M_OPENPBR_GLASS)
        glassTransmits = glassTransmits || q[4] > 0.f;
    }
    const std::string suffix = adobe ? " (useAdobeOpenPBR)" : "";
    check(same, "shadow-ray blockers match Typhoon's evaluation" + suffix);
    check(glassTransmits, "glass transmits shadow rays" + suffix);
    const auto viaGraphs = runShadow(in, n, adobe, g_ispcMaterials, false);
    bool sameViaGraphs = viaGraphs.size() == out.size();
    for (size_t j = 0; j < out.size() && sameViaGraphs; j++) {
      const int m = int(in[(j / SHADOW_STRIDE) * IN_STRIDE]);
      sameViaGraphs = matchesNative(out[j], viaGraphs[j], m, true);
    }
    check(sameViaGraphs,
        std::string("native shadow-ray blockers match their graphs ")
            + (BITWISE ? "exactly" : "within rounding") + suffix);
  }
}

// Each material gets the parts of the shading context its graph reads:
// sending every part to every material changes no bit (vertex and shadow),
// and leaving out a part changes the materials that read it
void checkContextGroups()
{
  const int n = 2048;
  const auto in = makePoints(n, PointOptions{}, 17);
  const auto sameBitsAll = [](const std::vector<float> &a,
                               const std::vector<float> &b) {
    return a.size() == b.size()
        && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
  };
  // handles differ from run to run only by the arena's generation
  const auto withoutHandles = [](std::vector<float> out) {
    for (size_t i = 0; i < out.size(); i += OUT_STRIDE)
      putInt(&out[i], 0, getInt(&out[i], 0) >= 0 ? 0 : -1);
    return out;
  };
  auto all = makeIspcMaterials([](int) { return ispc::PATH_CONTEXT_ALL; });
  check(sameBitsAll(withoutHandles(runVertex(in, n, false)),
            withoutHandles(runVertex(in, n, false, all)))
          && sameBitsAll(runShadow(in, n, false), runShadow(in, n, false, all)),
      "sending every part of the shading context changes nothing");

  const std::pair<int, uint32_t> readers[] = {
      {M_GEOMCOLOR, ispc::PATH_CONTEXT_DISPLAY_COLOR},
      {M_WORLD_POSITION, ispc::PATH_CONTEXT_TRANSFORMS},
      {M_BUMP, ispc::PATH_CONTEXT_SURFACE_DERIVATIVES},
      {M_BUMP, ispc::PATH_CONTEXT_SCREEN_DERIVATIVES},
      {M_SPLIT, ispc::PATH_CONTEXT_SCREEN_DERIVATIVES}};
  for (const auto &[m, group] : readers) {
    PointOptions o;
    o.material = m;
    const int points = 512;
    const auto in = makePoints(points, o, 19 + m);
    auto without = makeIspcMaterials(
        [group = group](int i) { return ownContextMask(i) & ~group; });
    const bool reads = (ownContextMask(m) & group)
        && !sameBitsAll(withoutHandles(runVertex(in, points, false)),
            withoutHandles(runVertex(in, points, false, without)));
    check(reads,
        g_materials[m].name + " reads PathContextGroup " + std::to_string(group)
            + ", and gets it");
  }
}

// Handles stay valid until the gang's next shading call with C++ lanes
void checkHandleLifetime()
{
  const int gang = ispc::test_programCount();
  PointOptions cpp;
  cpp.material = M_PREVIEW_EMISSIVE;
  cpp.pruneFraction = 0.f;
  PointOptions native;
  native.material = M_LAMBERT;
  const auto a = makePoints(gang, cpp, 21);
  const auto expected = runVertex(a, gang, false);

  std::vector<float> pdf(gang, -1.f);
  ispc::kernel_twoVertices(ispcMaterials(),
      a.data(),
      makePoints(gang, native, 22).data(),
      pdf.data());
  bool kept = true;
  int nonzero = 0;
  for (int i = 0; i < gang; i++) {
    kept = kept && sameBits(pdf[i], expected[size_t(i) * OUT_STRIDE + 22]);
    nonzero += pdf[i] > 0.f;
  }
  check(kept && nonzero > 0,
      "a gang without C++ lanes makes no call: earlier handles still evaluate");

  ispc::kernel_twoVertices(
      ispcMaterials(), a.data(), makePoints(gang, cpp, 23).data(), pdf.data());
  bool refused = true;
  for (int i = 0; i < gang; i++)
    refused = refused && pdf[i] == 0.f;
  check(refused, "handles from the gang's previous vertex are refused");
}

// A subsurface walk's exit replaces the material, whatever its kind
void checkSubsurfaceExit()
{
  PointOptions o;
  o.pruneFraction = 0.f;
  const int n = 512;
  auto in = makePoints(n, o, 31);
  for (int i = 0; i < n; i++) {
    float *p = &in[size_t(i) * IN_STRIDE];
    p[22] = float(uint32_t(p[22]) | ispc::PATH_SHADE_SUBSURFACE_EXIT);
  }
  const auto out = runVertex(in, n, false);
  bool same = true;
  for (int i = 0; i < n && same; i++) {
    const float *p = &in[size_t(i) * IN_STRIDE];
    Expected e;
    putInt(e.row.data(), 0, -1);
    e.row[8] = p[21];
    e.row[18] = 1.f;
    expectCpp(nullptr, p, false, true, e);
    const float *q = &out[size_t(i) * OUT_STRIDE];
    same = (getInt(q, 0) >= 0) == e.kept;
    for (int k = 1; k < OUT_STRIDE && same; k++)
      same = sameBits(q[k], e.row[k]);
    if (!same) {
      for (int k = 0; k < OUT_STRIDE; k++) {
        if (k > 0 && !sameBits(q[k], e.row[k]))
          std::printf("  point %d (material %d) element %d: %g vs %g\n",
              i,
              int(p[0]),
              k,
              q[k],
              e.row[k]);
      }
    }
  }
  check(same, "a subsurface exit shades the synthetic Lambertian");
}

// Many threads at once, each with its own arena, as Embree's tasking threads
// will call in; results must not depend on the thread or the interleaving
void checkThreads(int threads)
{
  const int n = 4096;
  const auto in = makePoints(n, PointOptions{}, 41);
  const auto ref = runVertex(in, n, false);

  std::vector<std::vector<float>> outs(threads);
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; t++)
    pool.emplace_back([&, t]() { outs[t] = runVertex(in, n, false); });
  for (auto &th : pool)
    th.join();

  bool same = true;
  for (int t = 0; t < threads; t++) {
    for (int i = 0; i < n && same; i++) {
      for (int k = 1; k < OUT_STRIDE; k++) {
        const size_t j = size_t(i) * OUT_STRIDE + k;
        same = same && sameBits(ref[j], outs[t][j]);
      }
    }
  }
  check(same,
      "concurrent shading on " + std::to_string(threads)
          + " threads matches one thread");
}

// Costs (--bench) ////////////////////////////////////////////////////////////

double bestSeconds(const std::function<void()> &fn, int repeats)
{
  double best = 1e30;
  for (int r = 0; r < repeats; r++) {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    best = std::min(best,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count());
  }
  return best;
}

void bench()
{
  const int n = 1 << 16, repeats = 5;
  std::printf(
      "TIME gang width %d, %d points, best of %d, ns per point: "
      "Typhoon's sequence in C++ | through the interface\n",
      ispc::test_programCount(),
      n,
      repeats);
  for (int m : {M_PREVIEW_EMISSIVE,
           M_OPENPBR_COAT,
           M_PREVIEW_NOISE,
           M_LAMBERT,
           M_MATTE_UNBOUNDED,
           M_PBM_DEFAULT,
           M_PBM_MIXED,
           M_PBM_SHEEN,
           M_PBM_COAT_NORMAL_MAPS}) {
    PointOptions o;
    o.material = m;
    o.pruneFraction = 0.f;
    const auto in = makePoints(n, o, 51 + m);
    volatile float sink = 0.f;
    const double direct = bestSeconds(
        [&]() {
          for (int i = 0; i < n; i++)
            sink =
                sink + expectVertex(&in[size_t(i) * IN_STRIDE], false).row[16];
        },
        repeats);
    std::vector<float> out(size_t(n) * OUT_STRIDE);
    const double ispc = bestSeconds(
        [&]() {
          ispc::kernel_vertex(
              false, true, ispcMaterials(), in.data(), out.data(), n);
        },
        repeats);
    std::printf("TIME %-36s %8.1f | %8.1f\n",
        g_materials[m].name.c_str(),
        1e9 * direct / n,
        1e9 * ispc / n);
    if (g_materials[m].kind == ispc::MATERIAL_KIND_MATTE
        || g_materials[m].kind == ispc::MATERIAL_KIND_PHYSICALLY_BASED) {
      const double graph = bestSeconds(
          [&]() {
            ispc::kernel_vertex(
                false, false, ispcMaterials(), in.data(), out.data(), n);
          },
          repeats);
      std::printf("TIME %-36s %8.1f | %8.1f (its graph, nativeMaterials "
                  "false)\n",
          g_materials[m].name.c_str(),
          1e9 * direct / n,
          1e9 * graph / n);
    }
  }
}

} // namespace

int main(int argc, const char **argv)
{
  const bool benchmark = argc > 1 && std::strcmp(argv[1], "--bench") == 0;

  const Vec3f luminance = halcyon::pathtracer::luminanceCoefficients();
  ispc::test_setLuminance(luminance[0], luminance[1], luminance[2]);
  check(makeMaterials(), "graphs compile");
  check(ispc::test_programCount() <= halcyon::pathtracer::MAX_LANES,
      "the gang fits the SoA records");
  if (g_failures)
    return 1;

  checkVertexAgainstTyphoon();
  checkNativeMatte();
  checkNativePhysicallyBased();
  checkCoverage();
  checkShadow();
  checkContextGroups();
  checkHandleLifetime();
  checkSubsurfaceExit();
  checkThreads(std::clamp(int(std::thread::hardware_concurrency()), 2, 8));

  if (benchmark)
    bench();

  return g_failures ? 1 : 0;
}
