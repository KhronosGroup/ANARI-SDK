// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The pathtracer's material graphs (renderer/pathtracer/MaterialConverter.*)
// and the geomprop bindings of the C++ materials they compile into
// (CppMaterial, renderer/pathtracer/PathMaterial.h):
//
// - matte is a Lambertian of its colour whose presence is its opacity after
//   'alphaMode';
// - physicallyBased evaluates to the same closure as a glTF PBR node with
//   each KHR parameter on its glTF input, for constants, attributes (resolved
//   by a geomprop lookup like the pathtracer's) and the defaults;
// - alpha is the colour's alpha times the opacity, as the other renderers;
// - sampler-sourced parameters are halcyon samplers (ISPC objects filled
//   with Sampler.ispc's setters, which the test links) through the texture
//   system: sampler nodes reading the sampler's inAttribute, MaterialX image
//   nodes with "anari:sampler/<index>" keys, normal maps, and the parameter's
//   value for an invalid sampler; only 'occlusion' is listed as unused;
// - each geomprop handle binds an attribute and its node's type, and the
//   material's attribute mask lists what it reads; attribute names are
//   helium's; a node reading an attribute the point lacks gives its default,
//   (0, 0, 0, 1) in the graphs halcyon builds;
// - the material's context mask lists the parts of the shading context its
//   nodes read (renderer/pathtracer/ContextGroups.*), and the audit behind
//   it was made at the vendored Typhoon commit.

#include "Sampler_ispc.h"
#include "renderer/pathtracer/ContextGroups.h"
#include "renderer/pathtracer/HalcyonTextureSystem.h"
#include "renderer/pathtracer/MaterialConverter.h"
#include "renderer/pathtracer/PathMaterial.h"
// helium
#include <helium/helium_math.h>
// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
// std
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mxcpp;
using namespace halcyon::pathtracer;

namespace {

constexpr float PI = 3.14159265358979323846f;

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok)
    g_failures++;
}

const std::vector<std::string> ATTRIBUTE_NAMES = {"attribute0",
    "attribute1",
    "attribute2",
    "attribute3",
    "color",
    "worldPosition",
    "worldNormal",
    "objectPosition",
    "objectNormal"};

// Attribute values at the shading point, by ispc::Attribute
using Attributes = std::array<Vec4f, 9>;

// Every attribute present at the shading point
constexpr uint32_t ALL_PRESENT = ~0u;

struct Lookup
{
  const CppMaterial *material;
  const Attributes *attributes;
  // The attributes the point has (bit (1 << a)), as
  // PathShadeInSoA::attributePresent
  uint32_t present;
};

// What the pathtracer's lookup (PathMaterial.cpp) does with the SoA rows:
// empty, the node's default, for a name that isn't an attribute or an
// attribute the point doesn't have
Value lookup(const void *userData, int handle)
{
  const auto &l = *static_cast<const Lookup *>(userData);
  const GeomPropBinding &b = l.material->geomProps().at(handle);
  if (b.attribute < 0 || !(l.present & (1u << b.attribute)))
    return {};
  const Vec4f &a = (*l.attributes)[b.attribute];
  switch (b.type) {
  case GeomPropType::FLOAT:
    return Value(a[0]);
  case GeomPropType::VECTOR2:
    return Value(Vec2f(a[0], a[1]));
  case GeomPropType::VECTOR3:
    return Value(Vec3f(a[0], a[1], a[2]));
  case GeomPropType::VECTOR4:
    return Value(a);
  case GeomPropType::INTEGER:
    return Value(int(a[0]));
  case GeomPropType::BOOLEAN:
    return Value(a[0] != 0.f);
  default:
    return {};
  }
}

std::unique_ptr<CppMaterial> compile(
    const MaterialGraph &graph, std::vector<const void *> samplers = {})
{
  std::string diagnostic;
  auto m = CppMaterial::compile(graph, std::move(samplers), &diagnostic);
  if (!m || !diagnostic.empty())
    std::printf("  compile: %s\n", diagnostic.c_str());
  return m;
}

std::unique_ptr<CppMaterial> compile(const ConvertedMaterial &converted)
{
  return compile(converted.graph, converted.samplers);
}

// The material at a point with 'attributes' (those in 'present' there) on
// Embree primitive 'faceId', with its texture system (as the pathtracer's
// shading context)
SurfaceClosure evaluate(const CppMaterial &material,
    const Attributes &attributes = {},
    int faceId = 0,
    uint32_t present = ALL_PRESENT)
{
  ShadingContext ctx;
  ctx.normal = Vec3f(0.f, 0.f, 1.f);
  ctx.tangent = Vec3f(1.f, 0.f, 0.f);
  ctx.bitangent = Vec3f(0.f, 1.f, 0.f);
  ctx.dPdu = Vec3f(1.f, 0.f, 0.f);
  ctx.dPdv = Vec3f(0.f, 1.f, 0.f);
  ctx.faceId = faceId;
  Lookup l{&material, &attributes, present};
  ctx.geomPropLookup = &lookup;
  ctx.geomPropUserData = &l;
  ctx.textureSystem = &material.textures();
  return material.graph().Evaluate(ctx);
}

// The closure's BSDF value times cosine towards a few directions, seen from
// one, as the pathtracer prepares it and once presence is resolved (the
// shading call sets it to 1 for a path that interacts)
std::vector<float> responses(SurfaceClosure closure)
{
  closure.presence = 1.f;
  const Vec3f N(0.f, 0.f, 1.f);
  const Vec3f wo = Vec3f(0.3f, -0.2f, 0.9f).normalized();
  Bsdf::detail::PrepareShadingNormals(&closure.bsdfTree, N, N, wo);
  SurfaceInteraction si;
  si.normalShdWldOut = N;
  si.normalSrfWldOut = N;
  si.normalGeomWldOut = N;
  si.omegaOutWld = wo;
  std::vector<float> r;
  for (const Vec3f &d : {Vec3f(0.f, 0.f, 1.f),
           Vec3f(-0.3f, 0.2f, 0.9f),
           Vec3f(0.5f, 0.5f, 0.3f),
           Vec3f(-0.8f, 0.1f, 0.2f),
           Vec3f(0.2f, 0.1f, -0.9f)}) {
    const Vec3f f = Bsdf::EvalSurfaceCosine(closure, si, d.normalized());
    r.insert(r.end(), {f[0], f[1], f[2]});
  }
  return r;
}

// The closure facts glTF PBR's evaluation sets
std::vector<float> summary(const SurfaceClosure &c)
{
  return {c.baseColor[0],
      c.baseColor[1],
      c.baseColor[2],
      c.metallic,
      c.roughness,
      c.specular,
      c.specularColor[0],
      c.specularColor[1],
      c.specularColor[2],
      c.specularIor,
      c.transmission,
      c.sheenColor[0],
      c.sheenColor[1],
      c.sheenColor[2],
      c.sheenRoughness,
      c.coat,
      c.coatRoughness,
      c.emissiveColor[0],
      c.emissiveColor[1],
      c.emissiveColor[2],
      c.opacity,
      c.presence,
      float(c.thinWalled)};
}

bool sameClosure(const SurfaceClosure &a, const SurfaceClosure &b)
{
  return summary(a) == summary(b) && responses(a) == responses(b);
}

MaterialGraph gltfNode(std::map<std::string, Value> parameters)
{
  MaterialGraph g;
  g.nodes["/gltf"].nodeTypeId = "ND_gltf_pbr_surfaceshader";
  g.nodes["/gltf"].parameters = std::move(parameters);
  g.terminals["surface"] = {"/gltf", "out"};
  return g;
}

// A physicallyBased material of distinctive constants, and the glTF PBR
// parameters it should become
PhysicallyBasedParameters distinctivePbm()
{
  PhysicallyBasedParameters p;
  p.baseColor = MaterialParameter::constant(Vec4f(0.7f, 0.5f, 0.3f, 1.f));
  p.metallic = MaterialParameter::constant(0.25f);
  p.roughness = MaterialParameter::constant(0.4f);
  p.emissive = MaterialParameter::constant(Vec3f(0.1f, 0.2f, 0.3f));
  p.specular = MaterialParameter::constant(0.8f);
  p.specularColor = MaterialParameter::constant(Vec3f(0.9f, 0.8f, 0.7f));
  p.clearcoat = MaterialParameter::constant(0.6f);
  p.clearcoatRoughness = MaterialParameter::constant(0.15f);
  p.transmission = MaterialParameter::constant(0.3f);
  p.ior = MaterialParameter::constant(1.7f);
  p.thickness = MaterialParameter::constant(0.5f);
  p.attenuationDistance = MaterialParameter::constant(2.f);
  p.attenuationColor = MaterialParameter::constant(Vec3f(0.9f, 0.5f, 0.1f));
  p.sheenColor = MaterialParameter::constant(Vec3f(0.4f, 0.3f, 0.2f));
  p.sheenRoughness = MaterialParameter::constant(0.6f);
  p.iridescence = MaterialParameter::constant(0.7f);
  p.iridescenceIor = MaterialParameter::constant(1.4f);
  p.iridescenceThickness = MaterialParameter::constant(350.f);
  return p;
}

std::map<std::string, Value> distinctiveGltf()
{
  return {{"base_color", Value(Vec3f(0.7f, 0.5f, 0.3f))},
      {"metallic", Value(0.25f)},
      {"roughness", Value(0.4f)},
      {"emissive", Value(Vec3f(0.1f, 0.2f, 0.3f))},
      {"specular", Value(0.8f)},
      {"specular_color", Value(Vec3f(0.9f, 0.8f, 0.7f))},
      {"clearcoat", Value(0.6f)},
      {"clearcoat_roughness", Value(0.15f)},
      {"transmission", Value(0.3f)},
      {"ior", Value(1.7f)},
      {"thickness", Value(0.5f)},
      {"attenuation_distance", Value(2.f)},
      {"attenuation_color", Value(Vec3f(0.9f, 0.5f, 0.1f))},
      {"sheen_color", Value(Vec3f(0.4f, 0.3f, 0.2f))},
      {"sheen_roughness", Value(0.6f)},
      {"iridescence", Value(0.7f)},
      {"iridescence_ior", Value(1.4f)},
      {"iridescence_thickness", Value(350.f)},
      {"alpha_mode", Value(0)},
      {"alpha_cutoff", Value(0.5f)}};
}

// Tests //////////////////////////////////////////////////////////////////////

void testAttributeNames()
{
  bool ok = true;
  for (const std::string &name : ATTRIBUTE_NAMES) {
    ok = ok
        && attributeFromName(name)
            == int(helium::attributeFromString(name));
  }
  for (const char *junk : {"", "none", "Color", "attribute4", "st"}) {
    ok = ok && attributeFromName(junk) == -1
        && helium::attributeFromString(junk) == helium::Attribute::NONE;
  }
  check(ok, "attribute names are helium's, in ISPC's order");
}

void testMatte()
{
  MatteParameters p;
  p.color = MaterialParameter::constant(Vec4f(0.6f, 0.4f, 0.2f, 1.f));
  auto m = compile(matteGraph(p));
  if (!m) {
    check(false, "matte compiles");
    return;
  }
  const SurfaceClosure c = evaluate(*m);
  const std::vector<float> r = responses(c);
  // Lambertian: colour / pi times the cosine, nothing below the surface
  bool lambert = c.presence == 1.f && m->attributeMask() == 0;
  const Vec3f dirs[] = {Vec3f(0.f, 0.f, 1.f),
      Vec3f(-0.3f, 0.2f, 0.9f).normalized(),
      Vec3f(0.5f, 0.5f, 0.3f).normalized(),
      Vec3f(-0.8f, 0.1f, 0.2f).normalized()};
  for (int i = 0; i < 4; i++) {
    const float cosine = dirs[i][2];
    lambert = lambert && std::abs(r[3 * i] - 0.6f / PI * cosine) < 1e-6f
        && std::abs(r[3 * i + 1] - 0.4f / PI * cosine) < 1e-6f
        && std::abs(r[3 * i + 2] - 0.2f / PI * cosine) < 1e-6f;
  }
  lambert = lambert && r[12] == 0.f && r[13] == 0.f && r[14] == 0.f;
  check(lambert, "matte is a Lambertian of its colour, opaque");
}

void testMatteAlpha()
{
  // blend: the colour's alpha times the opacity
  MatteParameters p;
  p.color = MaterialParameter::constant(Vec4f(0.5f, 0.5f, 0.5f, 0.5f));
  p.opacity = MaterialParameter::constant(0.6f);
  p.alphaMode = AlphaMode::BLEND;
  auto m = compile(matteGraph(p));
  check(m && std::abs(evaluate(*m).presence - 0.3f) < 1e-7f,
      "matte blend: presence is the colour's alpha times the opacity");

  p.alphaMode = AlphaMode::OPAQUE;
  m = compile(matteGraph(p));
  check(m && evaluate(*m).presence == 1.f, "matte opaque ignores alpha");

  // from attributes: colour rgb and alpha from 'color', opacity from
  // 'attribute2' (x)
  p.color = MaterialParameter::fromAttribute("color", Vec4f(1.f));
  p.opacity = MaterialParameter::fromAttribute("attribute2", Vec4f(1.f));
  p.alphaMode = AlphaMode::BLEND;
  m = compile(matteGraph(p));
  Attributes a{};
  a[4] = Vec4f(0.2f, 0.4f, 0.6f, 0.5f);
  a[2] = Vec4f(0.8f, 9.f, 9.f, 9.f);
  bool ok = m && m->attributeMask() == ((1u << 4) | (1u << 2));
  if (m) {
    const SurfaceClosure c = evaluate(*m, a);
    const std::vector<float> r = responses(c);
    ok = ok && std::abs(c.presence - 0.4f) < 1e-7f
        && std::abs(r[0] - 0.2f / PI) < 1e-6f
        && std::abs(r[1] - 0.4f / PI) < 1e-6f
        && std::abs(r[2] - 0.6f / PI) < 1e-6f;
  }
  check(ok, "matte colour and opacity from attributes");

  // mask: the clamped alpha against the cutoff, constant or not
  struct Case
  {
    float alpha;
    float cutoff;
    float presence;
  };
  bool masked = true;
  for (const Case &c : {Case{0.3f, 0.5f, 0.f},
           Case{0.7f, 0.5f, 1.f},
           Case{0.5f, 0.5f, 1.f},
           Case{2.f, 1.5f, 0.f},
           Case{-1.f, 0.f, 1.f}}) {
    MatteParameters q;
    q.alphaMode = AlphaMode::MASK;
    q.alphaCutoff = c.cutoff;
    q.opacity = MaterialParameter::constant(c.alpha);
    auto constant = compile(matteGraph(q));
    q.opacity = MaterialParameter::fromAttribute("attribute0", Vec4f(1.f));
    auto attribute = compile(matteGraph(q));
    Attributes v{};
    v[0] = Vec4f(c.alpha, 0.f, 0.f, 1.f);
    masked = masked && constant && attribute
        && evaluate(*constant).presence == c.presence
        && evaluate(*attribute, v).presence == c.presence;
  }
  check(masked, "matte mask: the clamped alpha against alphaCutoff");
}

void testPhysicallyBased()
{
  auto m = compile(physicallyBasedGraph(distinctivePbm()));
  auto expected = compile(gltfNode(distinctiveGltf()));
  check(m && expected && sameClosure(evaluate(*m), evaluate(*expected)),
      "physicallyBased: every KHR parameter on its glTF PBR input");

  // ANARI's defaults (specular 0, not glTF's 1)
  m = compile(physicallyBasedGraph(PhysicallyBasedParameters{}));
  expected = compile(gltfNode({{"base_color", Value(Vec3f(1.f))},
      {"metallic", Value(1.f)},
      {"roughness", Value(1.f)},
      {"emissive", Value(Vec3f(0.f))},
      {"specular", Value(0.f)},
      {"specular_color", Value(Vec3f(1.f))},
      {"clearcoat", Value(0.f)},
      {"clearcoat_roughness", Value(0.f)},
      {"transmission", Value(0.f)},
      {"ior", Value(1.5f)},
      {"thickness", Value(0.f)},
      {"sheen_color", Value(Vec3f(0.f))},
      {"sheen_roughness", Value(0.f)},
      {"iridescence", Value(0.f)},
      {"iridescence_ior", Value(1.3f)},
      {"iridescence_thickness", Value(0.f)}}));
  check(m && expected && sameClosure(evaluate(*m), evaluate(*expected)),
      "physicallyBased: ANARI's defaults");

  // every parameter from an attribute: the same as constants of the
  // attributes' values
  PhysicallyBasedParameters p = distinctivePbm();
  Attributes a{};
  a[0] = Vec4f(0.7f, 0.5f, 0.3f, 1.f); // baseColor
  a[1] = Vec4f(0.25f, 0.4f, 0.8f, 0.6f); // metallic (its x)
  a[2] = Vec4f(0.1f, 0.2f, 0.3f, 1.f); // emissive
  a[3] = Vec4f(0.9f, 0.8f, 0.7f, 1.f); // specularColor
  a[4] = Vec4f(0.4f, 0.3f, 0.2f, 1.f); // sheenColor
  p.baseColor = MaterialParameter::fromAttribute("attribute0", Vec4f(1.f));
  p.metallic = MaterialParameter::fromAttribute("attribute1", Vec4f(1.f));
  p.emissive = MaterialParameter::fromAttribute("attribute2", Vec4f(0.f));
  p.specularColor =
      MaterialParameter::fromAttribute("attribute3", Vec4f(1.f));
  p.sheenColor = MaterialParameter::fromAttribute("color", Vec4f(0.f));
  m = compile(physicallyBasedGraph(p));
  expected = compile(gltfNode(distinctiveGltf()));
  check(m && expected && m->attributeMask() == 0x1f
          && sameClosure(evaluate(*m, a), evaluate(*expected)),
      "physicallyBased: parameters from attributes");

  // a float parameter takes the attribute's x
  p = distinctivePbm();
  p.roughness = MaterialParameter::fromAttribute("worldNormal", Vec4f(1.f));
  a = {};
  a[6] = Vec4f(0.4f, 0.9f, 0.9f, 0.9f);
  m = compile(physicallyBasedGraph(p));
  check(m && m->attributeMask() == (1u << 6)
          && sameClosure(evaluate(*m, a), evaluate(*expected)),
      "physicallyBased: a float parameter from an attribute takes its x");
}

void testPhysicallyBasedAlpha()
{
  // blend: alpha from the base colour's alpha (an attribute's w) times the
  // opacity
  PhysicallyBasedParameters p = distinctivePbm();
  p.alphaMode = AlphaMode::BLEND;
  p.baseColor = MaterialParameter::fromAttribute("color", Vec4f(1.f));
  p.opacity = MaterialParameter::constant(0.5f);
  Attributes a{};
  a[4] = Vec4f(0.7f, 0.5f, 0.3f, 0.6f);
  auto m = compile(physicallyBasedGraph(p));
  auto params = distinctiveGltf();
  params["alpha_mode"] = Value(2);
  params["alpha"] = Value(0.3f);
  auto expected = compile(gltfNode(params));
  check(m && expected && sameClosure(evaluate(*m, a), evaluate(*expected))
          && std::abs(evaluate(*m, a).presence - 0.3f) < 1e-7f,
      "physicallyBased blend: alpha is the base colour's alpha times opacity");

  // mask
  p = distinctivePbm();
  p.alphaMode = AlphaMode::MASK;
  p.alphaCutoff = 0.4f;
  p.baseColor = MaterialParameter::constant(Vec4f(0.7f, 0.5f, 0.3f, 0.5f));
  p.opacity = MaterialParameter::constant(0.7f);
  m = compile(physicallyBasedGraph(p));
  check(m && evaluate(*m).presence == 0.f,
      "physicallyBased mask: 0.35 under the cutoff 0.4 is cut out");
  p.alphaCutoff = 0.3f;
  m = compile(physicallyBasedGraph(p));
  check(m && evaluate(*m).presence == 1.f,
      "physicallyBased mask: 0.35 over the cutoff 0.3 is opaque");
}

// Samplers ///////////////////////////////////////////////////////////////////

// An ISPC Sampler object, filled with Sampler.ispc's setters as the device's
// samplers fill theirs
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

  // inAttribute and in/out transforms (column-major) and offsets
  void setMapping(ispc::Attribute inAttribute,
      const std::array<float, 16> &inTransform = identity(),
      const Vec4f &inOffset = Vec4f(0.f),
      const std::array<float, 16> &outTransform = identity(),
      const Vec4f &outOffset = Vec4f(0.f))
  {
    const float in[4] = {inOffset[0], inOffset[1], inOffset[2], inOffset[3]};
    const float out[4] = {
        outOffset[0], outOffset[1], outOffset[2], outOffset[3]};
    ispc::Sampler_setParameters(
        storage, inAttribute, inTransform.data(), in, outTransform.data(), out);
  }

  // A nearest, clampToEdge image2D of 'width' x 'height' 'texels'
  void setImage2D(const std::vector<Vec4f> &texels, int width, int height)
  {
    const int size[3] = {width, height, 1};
    const ispc::WrapMode wrap[3] = {ispc::WRAP_MODE_CLAMP_TO_EDGE,
        ispc::WRAP_MODE_CLAMP_TO_EDGE,
        ispc::WRAP_MODE_CLAMP_TO_EDGE};
    ispc::Sampler_setImage2D(storage, &texels[0][0], size, wrap, false);
  }

  static std::array<float, 16> identity()
  {
    std::array<float, 16> m{};
    for (int i = 0; i < 4; i++)
      m[5 * i] = 1.f;
    return m;
  }
};

// 2 x 2 texels: (0, 0), (1, 0), (0, 1), (1, 1)
const std::vector<Vec4f> TEXELS = {Vec4f(0.1f, 0.2f, 0.3f, 1.f),
    Vec4f(0.7f, 0.5f, 0.3f, 0.6f),
    Vec4f(0.2f, 0.4f, 0.8f, 1.f),
    Vec4f(0.9f, 0.1f, 0.5f, 0.2f)};

bool near(const Vec4f &a, const Vec4f &b)
{
  for (int c = 0; c < 4; c++) {
    if (std::abs(a[c] - b[c]) > 1e-6f)
      return false;
  }
  return true;
}

void testTextureSystem()
{
  check(ispc::sizeof_Sampler() <= sizeof(IspcSampler::storage),
      "the test's sampler storage holds an ISPC Sampler");

  bool keys = HalcyonTextureSystem::samplerKey(0) == "anari:sampler/0"
      && HalcyonTextureSystem::samplerIndex("anari:sampler/12") == 12;
  for (const char *junk : {"anari:sampler/",
           "anari:sampler/1x",
           "anari:sampler/-1",
           "anari:sampler/+1",
           "anari:sampler/ 1",
           "anari:sampler/01",
           "anari:sampler/99999999999",
           "anari:Sampler/1",
           "texture.png",
           ""}) {
    keys = keys && HalcyonTextureSystem::samplerIndex(junk) == -1;
  }
  check(keys, "sampler keys: \"anari:sampler/<index>\", nothing else");

  IspcSampler image, scaled, primitive, invalid, noInput;
  noInput.setMapping(ispc::ATTRIBUTE_NONE);
  noInput.setImage2D(TEXELS, 2, 2);
  image.setMapping(ispc::ATTRIBUTE_0);
  image.setImage2D(TEXELS, 2, 2);
  // out: 2x + (0.5, 0, 0, 0)
  std::array<float, 16> twice = IspcSampler::identity();
  for (int i = 0; i < 4; i++)
    twice[5 * i] = 2.f;
  scaled.setMapping(ispc::ATTRIBUTE_0,
      IspcSampler::identity(),
      Vec4f(0.f),
      twice,
      Vec4f(0.5f, 0.f, 0.f, 0.f));
  scaled.setImage2D(TEXELS, 2, 2);
  const std::vector<Vec4f> values = {
      Vec4f(1.f), Vec4f(0.25f, 0.5f, 0.75f, 1.f), Vec4f(0.5f)};
  ispc::Sampler_setPrimitive(primitive.get(), &values[0][0], 3, 1);
  const HalcyonTextureSystem textures({image.get(),
      scaled.get(),
      primitive.get(),
      invalid.get(),
      noInput.get()});

  Texture2DRequest request;
  request.defaultValue = Vec4f(0.3f, 0.3f, 0.3f, 0.3f);
  request.filePath = "anari:sampler/0";
  request.st = Vec2f(0.75f, 0.25f);
  Texture2DResult r = textures.Sample2D(request);
  bool ok = r.status == TextureSampleStatus::Ok && near(r.value, TEXELS[1]);
  request.st = Vec2f(0.25f, 0.75f);
  r = textures.Sample2D(request);
  ok = ok && r.status == TextureSampleStatus::Ok && near(r.value, TEXELS[2]);
  check(ok, "Sample2D: st is the sampler's input, the first texel at 0");

  request.filePath = "anari:sampler/1";
  r = textures.Sample2D(request);
  ok = r.status == TextureSampleStatus::Ok
      && near(r.value, TEXELS[2] * 2.f + Vec4f(0.5f, 0.f, 0.f, 0.f));
  check(ok, "Sample2D: the sampler's output transform and offset");

  ok = true;
  for (const char *key : {"anari:sampler/2",
           "anari:sampler/3",
           "anari:sampler/4",
           "anari:sampler/5",
           "anari:sampler/x"}) {
    request.filePath = key;
    r = textures.Sample2D(request);
    ok = ok && r.status == TextureSampleStatus::Missing
        && r.value == request.defaultValue;
  }
  check(ok,
      "Sample2D: primitive, invalid, input-less and missing samplers are "
      "Missing, with the default value");
  request.filePath = "texture.png";
  r = textures.Sample2D(request);
  check(r.status == TextureSampleStatus::Error
          && r.value == request.defaultValue,
      "Sample2D: a file the material didn't read is an Error, with the "
      "default value");

  Vec4f v(-1.f);
  ok = textures.sample(2, Vec4f(0.f), 1, v) && v == values[2]
      && textures.sample(2, Vec4f(0.f), 0, v) && v == values[1]
      && !textures.sample(3, Vec4f(0.f), 0, v) && v == values[1]
      && !textures.sample(5, Vec4f(0.f), 0, v);
  check(ok,
      "sample(): a primitive sampler's value at the primitive plus its "
      "offset; invalid samplers give none");
  check(samplerInputAttribute(image.get()) == ispc::ATTRIBUTE_0
          && samplerInputAttribute(primitive.get()) == -1
          && samplerInputAttribute(invalid.get()) == -1,
      "a sampler's input attribute: none for primitive and invalid ones");
}

void testSamplerParameters()
{
  // baseColor from an image on attribute0, roughness the y of attribute2
  // (a transform sampler swizzling y into x), metallic per primitive
  IspcSampler image, swizzle, primitive, invalid;
  image.setMapping(ispc::ATTRIBUTE_0);
  image.setImage2D(TEXELS, 2, 2);
  std::array<float, 16> yToX{};
  yToX[4] = 1.f; // column 1, row 0
  swizzle.setMapping(
      ispc::ATTRIBUTE_2, IspcSampler::identity(), Vec4f(0.f), yToX);
  ispc::Sampler_setTransform(swizzle.get());
  const std::vector<Vec4f> values = {
      Vec4f(9.f), Vec4f(9.f), Vec4f(0.25f, 9.f, 9.f, 9.f)};
  ispc::Sampler_setPrimitive(primitive.get(), &values[0][0], 3, 1);

  PhysicallyBasedParameters p = distinctivePbm();
  p.baseColor = MaterialParameter::fromSampler(image.get(), Vec4f(1.f));
  p.roughness = MaterialParameter::fromSampler(swizzle.get(), Vec4f(1.f));
  p.metallic = MaterialParameter::fromSampler(primitive.get(), Vec4f(1.f));
  Attributes a{};
  a[0] = Vec4f(0.75f, 0.25f, 0.f, 1.f); // texel (1, 0): (0.7, 0.5, 0.3)
  a[2] = Vec4f(9.f, 0.4f, 9.f, 9.f);
  const ConvertedMaterial converted = physicallyBasedGraph(p);
  auto m = compile(converted);
  auto expected = compile(gltfNode(distinctiveGltf()));
  check(m && expected && converted.samplers.size() == 3
          && m->attributeMask() == ((1u << 0) | (1u << 2))
          && sameClosure(evaluate(*m, a, 1), evaluate(*expected)),
      "physicallyBased: parameters from image, transform and primitive "
      "samplers on their inAttributes");

  // an invalid sampler gives the parameter's value, and reads no attribute
  p = distinctivePbm();
  p.baseColor = MaterialParameter::fromSampler(
      invalid.get(), Vec4f(0.7f, 0.5f, 0.3f, 1.f));
  p.roughness = MaterialParameter::fromSampler(invalid.get(), Vec4f(0.4f));
  m = compile(physicallyBasedGraph(p));
  check(m && m->attributeMask() == 0
          && sameClosure(evaluate(*m), evaluate(*expected)),
      "an invalid sampler is the parameter's value");

  // blend: the colour sampler's alpha (one sampler node for the colour and
  // its alpha) times the opacity sampler's x
  MatteParameters q;
  q.color = MaterialParameter::fromSampler(image.get(), Vec4f(1.f));
  q.opacity = MaterialParameter::fromSampler(swizzle.get(), Vec4f(1.f));
  q.alphaMode = AlphaMode::BLEND;
  const ConvertedMaterial matte = matteGraph(q);
  int samplerNodes = 0;
  for (const auto &n : matte.graph.nodes)
    samplerNodes += n.second.nodeTypeId == SAMPLER_NODE_TYPE;
  m = compile(matte);
  a[2] = Vec4f(9.f, 0.5f, 9.f, 9.f);
  check(m && samplerNodes == 2
          && std::abs(evaluate(*m, a).presence - 0.6f * 0.5f) < 1e-6f,
      "matte blend: the colour sampler's alpha times the opacity sampler's");

  check(unsupportedSamplerParameters(p).empty()
          && unsupportedSamplerParameters(q).empty(),
      "sampler-sourced parameters are all used");
  p.occlusion = image.get();
  check(
      unsupportedSamplerParameters(p) == std::vector<std::string>{"occlusion"},
      "'occlusion' is listed as not used");
}

// A MaterialX image node naming a sampler by key, its texture coordinates
// from attribute0, as MaterialX materials name samplers
void testImageNode()
{
  IspcSampler image;
  image.setMapping(ispc::ATTRIBUTE_0);
  image.setImage2D(TEXELS, 2, 2);

  const auto graph = [](const std::string &file) {
    MaterialGraph g;
    auto &st = g.nodes["/st"];
    st.nodeTypeId = "ND_geompropvalue_vector2";
    st.parameters["geomprop"] = Value(std::string("attribute0"));
    auto &img = g.nodes["/image"];
    img.nodeTypeId = "ND_image_color3";
    img.parameters["file"] = Value(file);
    img.parameters["default"] = Value(Vec3f(0.3f));
    img.inputConnections["texcoord"] = {{"/st", "out"}};
    auto &surface = g.nodes["/surface"];
    surface.nodeTypeId = "ND_gltf_pbr_surfaceshader";
    surface.inputConnections["base_color"] = {{"/image", "out"}};
    g.terminals["surface"] = {"/surface", "out"};
    return g;
  };
  Attributes a{};
  a[0] = Vec4f(0.75f, 0.75f, 0.f, 1.f); // texel (1, 1)
  auto m = compile(graph("anari:sampler/0"), {image.get()});
  check(m
          && evaluate(*m, a).baseColor
              == Vec3f(TEXELS[3][0], TEXELS[3][1], TEXELS[3][2]),
      "an image node samples the sampler its key names at its texcoord");
  m = compile(graph("texture.png"), {image.get()});
  check(m && evaluate(*m, a).baseColor == Vec3f(0.3f),
      "an image node of a file it can't read is its default");
}

// The normal maps become ND_normalmap_float nodes on glTF PBR's normal and
// clearcoat_normal; without a clearcoat normal map, the clearcoat has the
// unperturbed normal
void testNormalMaps()
{
  IspcSampler map, clearcoatMap;
  const auto input = [](const ConvertedMaterial &c, const char *name) {
    const auto &surface = c.graph.nodes.at("/Material/gltf_pbr");
    const auto it = surface.inputConnections.find(name);
    if (it == surface.inputConnections.end() || it->second.empty())
      return std::string();
    return c.graph.nodes.at(it->second[0].upstreamNode).nodeTypeId;
  };

  PhysicallyBasedParameters p;
  ConvertedMaterial c = physicallyBasedGraph(p);
  bool ok = input(c, "normal").empty() && input(c, "clearcoat_normal").empty();
  p.normal = map.get();
  c = physicallyBasedGraph(p);
  ok = ok && input(c, "normal") == "ND_normalmap_float"
      && input(c, "clearcoat_normal") == "ND_normal_vector3" && compile(c);
  p.clearcoatNormal = clearcoatMap.get();
  c = physicallyBasedGraph(p);
  ok = ok && input(c, "normal") == "ND_normalmap_float"
      && input(c, "clearcoat_normal") == "ND_normalmap_float"
      && c.samplers.size() == 2 && compile(c);
  p.normal = nullptr;
  c = physicallyBasedGraph(p);
  ok = ok && input(c, "normal").empty()
      && input(c, "clearcoat_normal") == "ND_normalmap_float";
  check(ok, "normal maps on glTF PBR's normal and clearcoat_normal");
}

// openPBR and usdPreviewSurface ////////////////////////////////////////////

MaterialGraph modelNode(
    const std::string &type, std::map<std::string, Value> parameters)
{
  MaterialGraph g;
  g.nodes["/model"].nodeTypeId = type;
  g.nodes["/model"].parameters = std::move(parameters);
  g.terminals["surface"] = {"/model", "out"};
  return g;
}

// Distinct values for every input of 'inputs' (no default's), and the node
// parameters they should become
void distinctiveInputs(const std::vector<ShaderInput> &inputs,
    std::vector<MaterialParameter> &values,
    std::map<std::string, Value> &parameters)
{
  values.clear();
  for (size_t i = 0; i < inputs.size(); i++) {
    const float v = 0.11f + 0.017f * float(i);
    if (inputs[i].type == ShaderInput::Type::COLOR3) {
      const Vec3f c(v, 0.5f * v, 1.f - v);
      values.push_back(MaterialParameter::constant(c));
      parameters[inputs[i].name] = Value(c);
    } else {
      values.push_back(MaterialParameter::constant(v));
      parameters[inputs[i].name] = Value(v);
    }
  }
}

// The node an input of 'converted''s model node is connected to ("" for
// none)
std::string upstreamType(const ConvertedMaterial &converted,
    const std::string &model,
    const char *input)
{
  const auto &n = converted.graph.nodes.at(model);
  const auto it = n.inputConnections.find(input);
  if (it == n.inputConnections.end() || it->second.empty())
    return {};
  return converted.graph.nodes.at(it->second[0].upstreamNode).nodeTypeId;
}

void testOpenPbr()
{
  const std::string OPEN_PBR = "ND_open_pbr_surface_surfaceshader";
  OpenPbrParameters p;
  std::map<std::string, Value> params;
  distinctiveInputs(openPbrInputs(), p.inputs, params);
  params["geometry_thin_walled"] = Value(false);
  auto m = compile(openPbrGraph(p));
  auto expected = compile(modelNode(OPEN_PBR, params));
  check(m && expected && sameClosure(evaluate(*m), evaluate(*expected)),
      "openPBR: every input from the parameter of its name");

  // MaterialX's nodedef defaults (open_pbr_surface.mtlx, OpenPBR 1.1)
  m = compile(openPbrGraph(OpenPbrParameters{}));
  expected = compile(modelNode(OPEN_PBR,
      {{"base_weight", Value(1.f)},
          {"base_color", Value(Vec3f(0.8f))},
          {"base_diffuse_roughness", Value(0.f)},
          {"base_metalness", Value(0.f)},
          {"specular_weight", Value(1.f)},
          {"specular_color", Value(Vec3f(1.f))},
          {"specular_roughness", Value(0.3f)},
          {"specular_ior", Value(1.5f)},
          {"specular_roughness_anisotropy", Value(0.f)},
          {"transmission_weight", Value(0.f)},
          {"transmission_color", Value(Vec3f(1.f))},
          {"transmission_depth", Value(0.f)},
          {"transmission_scatter", Value(Vec3f(0.f))},
          {"transmission_scatter_anisotropy", Value(0.f)},
          {"transmission_dispersion_scale", Value(0.f)},
          {"transmission_dispersion_abbe_number", Value(20.f)},
          {"subsurface_weight", Value(0.f)},
          {"subsurface_color", Value(Vec3f(0.8f))},
          {"subsurface_radius", Value(1.f)},
          {"subsurface_radius_scale", Value(Vec3f(1.f, 0.5f, 0.25f))},
          {"subsurface_scatter_anisotropy", Value(0.f)},
          {"fuzz_weight", Value(0.f)},
          {"fuzz_color", Value(Vec3f(1.f))},
          {"fuzz_roughness", Value(0.5f)},
          {"coat_weight", Value(0.f)},
          {"coat_color", Value(Vec3f(1.f))},
          {"coat_roughness", Value(0.f)},
          {"coat_roughness_anisotropy", Value(0.f)},
          {"coat_ior", Value(1.6f)},
          {"coat_darkening", Value(1.f)},
          {"thin_film_weight", Value(0.f)},
          {"thin_film_thickness", Value(0.5f)},
          {"thin_film_ior", Value(1.4f)},
          {"emission_luminance", Value(0.f)},
          {"emission_color", Value(Vec3f(1.f))},
          {"geometry_opacity", Value(1.f)},
          {"geometry_thin_walled", Value(false)}}));
  check(m && expected && m->contextMask() == 0 && m->attributeMask() == 0
          && sameClosure(evaluate(*m), evaluate(*expected)),
      "openPBR: MaterialX's defaults, reading no optional context");

  // attributes: a colour's xyz, a float's x
  OpenPbrParameters q = p;
  openPbrInput(q, "base_color") =
      MaterialParameter::fromAttribute("attribute0", Vec4f(0.8f));
  openPbrInput(q, "specular_roughness") =
      MaterialParameter::fromAttribute("attribute1", Vec4f(0.3f));
  Attributes a{};
  a[0] = Vec4f(0.7f, 0.5f, 0.3f, 0.2f);
  a[1] = Vec4f(0.45f, 9.f, 9.f, 9.f);
  params["base_color"] = Value(Vec3f(0.7f, 0.5f, 0.3f));
  params["specular_roughness"] = Value(0.45f);
  m = compile(openPbrGraph(q));
  expected = compile(modelNode(OPEN_PBR, params));
  check(m && expected && m->attributeMask() == 0x3
          && sameClosure(evaluate(*m, a), evaluate(*expected)),
      "openPBR: inputs from attributes");

  q = OpenPbrParameters{};
  q.thinWalled = true;
  m = compile(openPbrGraph(q));
  check(m && evaluate(*m).thinWalled, "openPBR: geometry_thin_walled");

  // geometry_normal and geometry_coat_normal: world-space vector3s, as the
  // nodedef's (constants, attributes or samplers, used as given), so each
  // material evaluates as the model node with that value set; the coat keeps
  // the unperturbed normal (the nodedef's Nworld, (0, 0, 1) here) unless it
  // has its own. p's distinctive inputs have a coat.
  const std::string model = "/Material/open_pbr";
  const Vec3f tilted(0.6f, 0.f, 0.8f), coatTilted(0.f, 0.8f, 0.6f);
  const auto sameAsNode = [&](const OpenPbrParameters &q,
                              const Attributes &a,
                              const Vec3f &normal,
                              const Vec3f &coatNormal) {
    // p's node parameters (not params, which the attribute test changed)
    std::vector<MaterialParameter> unused;
    std::map<std::string, Value> withNormals;
    distinctiveInputs(openPbrInputs(), unused, withNormals);
    withNormals["geometry_thin_walled"] = Value(false);
    withNormals["geometry_normal"] = Value(normal);
    withNormals["geometry_coat_normal"] = Value(coatNormal);
    const auto m = compile(openPbrGraph(q));
    const auto expected = compile(modelNode(OPEN_PBR, withNormals));
    if (!m || !expected)
      return false;
    // the closure's own normal (which the integrator resolves, so
    // responses() doesn't see it) and the coat lobe's
    const SurfaceClosure got = evaluate(*m, a), want = evaluate(*expected);
    return sameClosure(got, want) && got.normal == normal
        && got.normalSpace == SurfaceNormalSpace::World;
  };
  q = p;
  ConvertedMaterial c = openPbrGraph(q);
  bool ok = upstreamType(c, model, "geometry_normal").empty()
      && upstreamType(c, model, "geometry_coat_normal").empty()
      && !c.graph.nodes.at(model).parameters.count("geometry_normal")
      && !c.graph.nodes.at(model).parameters.count("geometry_coat_normal");
  check(ok, "openPBR: unset normals are the nodedef's");

  q.normal = MaterialParameter::constant(tilted);
  c = openPbrGraph(q);
  check(upstreamType(c, model, "geometry_coat_normal") == "ND_normal_vector3"
          && sameAsNode(q, {}, tilted, Vec3f(0.f, 0.f, 1.f)),
      "openPBR: a constant geometry_normal is a world-space vector, the coat"
      " keeps the unperturbed normal");

  Attributes n{};
  n[2] = Vec4f(tilted[0], tilted[1], tilted[2], 1.f);
  q.normal = MaterialParameter::fromAttribute("attribute2", Vec4f(0.f));
  m = compile(openPbrGraph(q));
  check(m && m->attributeMask() == 0x4
          && sameAsNode(q, n, tilted, Vec3f(0.f, 0.f, 1.f)),
      "openPBR: geometry_normal from an attribute, a world-space vector");

  IspcSampler map; // reads the texels in place
  const std::vector<Vec4f> texel = {
      Vec4f(tilted[0], tilted[1], tilted[2], 1.f)};
  map.setMapping(ispc::ATTRIBUTE_0);
  map.setImage2D(texel, 1, 1);
  q.normal = MaterialParameter::fromSampler(map.get(), Vec4f(0.f));
  c = openPbrGraph(q);
  check(c.samplers.size() == 1
          && sameAsNode(q, {}, tilted, Vec3f(0.f, 0.f, 1.f)),
      "openPBR: geometry_normal from a sampler, its value as given");

  q.coatNormal = MaterialParameter::constant(coatTilted);
  check(sameAsNode(q, {}, tilted, coatTilted),
      "openPBR: geometry_coat_normal, a world-space vector");
  q.normal.reset();
  c = openPbrGraph(q);
  check(upstreamType(c, model, "geometry_normal").empty()
          && !c.graph.nodes.at(model).parameters.count("geometry_normal")
          && std::get<Vec3f>(
                 c.graph.nodes.at(model).parameters.at("geometry_coat_normal"))
              == coatTilted
          && compile(c),
      "openPBR: geometry_coat_normal alone");

  // tangents: the surface's (Tworld) only where a lobe is anisotropic, else
  // the parameter's
  q = OpenPbrParameters{};
  openPbrInput(q, "specular_roughness_anisotropy") =
      MaterialParameter::constant(0.5f);
  c = openPbrGraph(q);
  m = compile(c);
  ok = m && upstreamType(c, model, "geometry_tangent") == "ND_tangent_vector3"
      && upstreamType(c, model, "geometry_coat_tangent").empty()
      && m->contextMask() == ispc::PATH_CONTEXT_SURFACE_DERIVATIVES;
  q = OpenPbrParameters{};
  openPbrInput(q, "coat_roughness_anisotropy") =
      MaterialParameter::fromAttribute("attribute2", Vec4f(0.f));
  q.tangent = MaterialParameter::constant(Vec3f(0.f, 1.f, 0.f));
  c = openPbrGraph(q);
  ok = ok && upstreamType(c, model, "geometry_tangent").empty()
      && std::get<Vec3f>(c.graph.nodes.at(model).parameters.at(
             "geometry_tangent"))
          == Vec3f(0.f, 1.f, 0.f)
      && upstreamType(c, model, "geometry_coat_tangent")
          == "ND_tangent_vector3";
  q.coatTangent = MaterialParameter::fromAttribute("attribute3", Vec4f(0.f));
  c = openPbrGraph(q);
  ok = ok
      && upstreamType(c, model, "geometry_coat_tangent")
          == "ND_convert_vector4_vector3"
      && compile(c);
  check(ok, "openPBR: tangents, the surface's only for anisotropic lobes");
}

void testUsdPreviewSurface()
{
  const std::string USD = "ND_UsdPreviewSurface_surfaceshader";
  UsdPreviewSurfaceParameters p;
  std::map<std::string, Value> params;
  distinctiveInputs(usdPreviewSurfaceInputs(), p.inputs, params);
  params["useSpecularWorkflow"] = Value(0);
  params["opacityMode"] = Value(0);
  params["opacityThreshold"] = Value(0.f);
  auto m = compile(usdPreviewSurfaceGraph(p));
  auto expected = compile(modelNode(USD, params));
  check(m && expected && sameClosure(evaluate(*m), evaluate(*expected)),
      "usdPreviewSurface: every input from the parameter of its name");

  // the nodedef's defaults (usd_preview_surface.mtlx)
  m = compile(usdPreviewSurfaceGraph(UsdPreviewSurfaceParameters{}));
  expected = compile(modelNode(USD,
      {{"diffuseColor", Value(Vec3f(0.18f))},
          {"emissiveColor", Value(Vec3f(0.f))},
          {"useSpecularWorkflow", Value(0)},
          {"specularColor", Value(Vec3f(0.f))},
          {"metallic", Value(0.f)},
          {"roughness", Value(0.5f)},
          {"clearcoat", Value(0.f)},
          {"clearcoatRoughness", Value(0.01f)},
          {"opacity", Value(1.f)},
          {"opacityMode", Value(0)},
          {"opacityThreshold", Value(0.f)},
          {"ior", Value(1.5f)}}));
  check(m && expected && m->contextMask() == 0 && m->attributeMask() == 0
          && sameClosure(evaluate(*m), evaluate(*expected)),
      "usdPreviewSurface: the nodedef's defaults, reading no optional "
      "context");

  // the constants-only inputs
  UsdPreviewSurfaceParameters q = p;
  q.useSpecularWorkflow = true;
  q.opacityMode = UsdOpacityMode::PRESENCE;
  params["useSpecularWorkflow"] = Value(1);
  params["opacityMode"] = Value(1);
  m = compile(usdPreviewSurfaceGraph(q));
  expected = compile(modelNode(USD, params));
  const float opacity = std::get<float>(params["opacity"]);
  check(m && expected && sameClosure(evaluate(*m), evaluate(*expected))
          && evaluate(*m).presence == opacity,
      "usdPreviewSurface: specular workflow, opacity as presence");
  q.opacityMode = UsdOpacityMode::TRANSPARENT;
  m = compile(usdPreviewSurfaceGraph(q));
  check(m && evaluate(*m).presence == 1.f
          && std::abs(evaluate(*m).transmission - (1.f - opacity)) < 1e-6f,
      "usdPreviewSurface: opacity as transmission");
  q.opacityThreshold = opacity + 0.01f;
  m = compile(usdPreviewSurfaceGraph(q));
  bool cut = m && evaluate(*m).presence == 0.f;
  q.opacityThreshold = opacity - 0.01f;
  m = compile(usdPreviewSurfaceGraph(q));
  cut = cut && m && evaluate(*m).presence == 1.f;
  check(cut, "usdPreviewSurface: opacityThreshold cuts out");

  // 'normal': in tangent space, only where set; from a constant, an
  // attribute or a sampler, as a vector3
  const std::string model = "/Material/usd_preview_surface";
  const Vec3f tilted(0.6f, 0.f, 0.8f);
  bool ok = usdPreviewSurfaceGraph(UsdPreviewSurfaceParameters{})
                .graph.nodes.at(model)
                .parameters.count("normal")
      == 0;
  params = {{"normal", Value(tilted)}};
  expected = compile(modelNode(USD, params));
  q = UsdPreviewSurfaceParameters{};
  q.normal = MaterialParameter::constant(tilted);
  m = compile(usdPreviewSurfaceGraph(q));
  ok = ok && m && expected && sameClosure(evaluate(*m), evaluate(*expected));
  q.normal = MaterialParameter::fromAttribute("attribute3", Vec4f(0.f));
  const ConvertedMaterial fromAttribute = usdPreviewSurfaceGraph(q);
  m = compile(fromAttribute);
  Attributes a{};
  a[3] = Vec4f(tilted[0], tilted[1], tilted[2], 9.f);
  ok = ok && m
      && upstreamType(fromAttribute, model, "normal")
          == "ND_convert_vector4_vector3"
      && sameClosure(evaluate(*m, a), evaluate(*expected));
  IspcSampler image;
  image.setMapping(ispc::ATTRIBUTE_0);
  const std::vector<Vec4f> texel = {Vec4f(tilted[0], tilted[1], tilted[2], 1.f)};
  image.setImage2D(texel, 1, 1);
  q.normal = MaterialParameter::fromSampler(image.get(), Vec4f(0.f));
  m = compile(usdPreviewSurfaceGraph(q));
  ok = ok && m && sameClosure(evaluate(*m), evaluate(*expected));
  check(ok, "usdPreviewSurface: a tangent-space normal from a constant, "
            "an attribute or a sampler");
}

void testGeomPropBindings()
{
  // one name read as two types, and a name that isn't an attribute
  MaterialGraph g;
  auto &f = g.nodes["/f"];
  f.nodeTypeId = "ND_geompropvalue_float";
  f.parameters["geomprop"] = Value(std::string("attribute1"));
  f.parameters["default"] = Value(0.5f);
  auto &c = g.nodes["/c"];
  c.nodeTypeId = "ND_geompropvalue_color3";
  c.parameters["geomprop"] = Value(std::string("attribute1"));
  auto &st = g.nodes["/st"];
  st.nodeTypeId = "ND_geompropvalue_vector2";
  st.parameters["geomprop"] = Value(std::string("st"));
  auto &surface = g.nodes["/surface"];
  surface.nodeTypeId = "ND_gltf_pbr_surfaceshader";
  surface.inputConnections["base_color"] = {{"/c", "out"}};
  surface.inputConnections["roughness"] = {{"/f", "out"}};
  surface.inputConnections["metallic"] = {{"/m", "out"}};
  auto &metal = g.nodes["/m"];
  metal.nodeTypeId = "ND_extract_vector2";
  metal.inputConnections["in"] = {{"/st", "out"}};
  g.terminals["surface"] = {"/surface", "out"};

  auto m = compile(g);
  bool ok = m && m->attributeMask() == (1u << 1)
      && m->geomProps().size() == 3;
  if (ok) {
    int floats = 0, vectors = 0, unbound = 0;
    for (const GeomPropBinding &b : m->geomProps()) {
      floats += b.attribute == 1 && b.type == GeomPropType::FLOAT;
      vectors += b.attribute == 1 && b.type == GeomPropType::VECTOR3;
      unbound += b.attribute == -1;
    }
    ok = floats == 1 && vectors == 1 && unbound == 1;
  }
  if (ok) {
    Attributes a{};
    a[1] = Vec4f(0.25f, 0.5f, 0.75f, 1.f);
    const SurfaceClosure closure = evaluate(*m, a);
    // the colour reads xyz, the float x; 'st' isn't an attribute, so its
    // node gives its default (0, 0) and metallic is 0
    ok = closure.baseColor == Vec3f(0.25f, 0.5f, 0.75f)
        && closure.roughness == 0.25f && closure.metallic == 0.f;
  }
  check(ok, "geomprop handles bind an attribute and their node's type");
}

// Attributes a point has or lacks: a geomprop node
// reading an attribute the point lacks gives its default, as MaterialX and
// Typhoon give for a primvar the mesh lacks; halcyon's own geomprop nodes
// (attribute parameters, samplers' inAttribute) have the default (0, 0, 0, 1),
// what every renderer reads for a missing attribute
void testGeomPropPresence()
{
  // a document's nodes: a colour and a float of authored defaults, and a
  // name that isn't an attribute
  MaterialGraph g;
  auto &c = g.nodes["/c"];
  c.nodeTypeId = "ND_geompropvalue_color3";
  c.parameters["geomprop"] = Value(std::string("attribute1"));
  c.parameters["default"] = Value(Vec3f(0.1f, 0.2f, 0.3f));
  auto &f = g.nodes["/f"];
  f.nodeTypeId = "ND_geompropvalue_float";
  f.parameters["geomprop"] = Value(std::string("attribute2"));
  f.parameters["default"] = Value(0.6f);
  auto &st = g.nodes["/st"];
  st.nodeTypeId = "ND_geompropvalue_float";
  st.parameters["geomprop"] = Value(std::string("st"));
  st.parameters["default"] = Value(0.35f);
  auto &surface = g.nodes["/surface"];
  surface.nodeTypeId = "ND_gltf_pbr_surfaceshader";
  surface.inputConnections["base_color"] = {{"/c", "out"}};
  surface.inputConnections["roughness"] = {{"/f", "out"}};
  surface.inputConnections["metallic"] = {{"/st", "out"}};
  g.terminals["surface"] = {"/surface", "out"};
  auto m = compile(g);
  Attributes a{};
  a[1] = Vec4f(0.7f, 0.5f, 0.3f, 1.f);
  a[2] = Vec4f(0.25f, 9.f, 9.f, 9.f);
  bool ok = m && m->attributeMask() == ((1u << 1) | (1u << 2));
  if (ok) {
    const SurfaceClosure present = evaluate(*m, a);
    const SurfaceClosure absent = evaluate(*m, a, 0, ~((1u << 1) | (1u << 2)));
    const SurfaceClosure some = evaluate(*m, a, 0, ~(1u << 2));
    ok = present.baseColor == Vec3f(0.7f, 0.5f, 0.3f)
        && present.roughness == 0.25f && present.metallic == 0.35f
        && absent.baseColor == Vec3f(0.1f, 0.2f, 0.3f)
        && absent.roughness == 0.6f && absent.metallic == 0.35f
        && some.baseColor == Vec3f(0.7f, 0.5f, 0.3f)
        && some.roughness == 0.6f;
  }
  check(ok,
      "geomprop nodes: an attribute the point has, else (absent or not an "
      "attribute) the node's default");

  // halcyon's graphs: a parameter from attribute3, absent, is the parameter
  // from attribute3 of (0, 0, 0, 1), as the preview renderer reads it; also
  // a sampler on attribute3
  IspcSampler image;
  image.setMapping(ispc::ATTRIBUTE_3);
  image.setImage2D(TEXELS, 2, 2);
  const MaterialParameter attribute = MaterialParameter::fromAttribute(
      "attribute3", Vec4f(0.9f, 0.8f, 0.7f, 1.f));
  const MaterialParameter sampler =
      MaterialParameter::fromSampler(image.get(), Vec4f(0.9f));
  MatteParameters matte;
  matte.color = attribute;
  matte.opacity = attribute;
  matte.alphaMode = AlphaMode::BLEND;
  PhysicallyBasedParameters pbm = distinctivePbm();
  pbm.baseColor = sampler;
  pbm.roughness = attribute;
  pbm.metallic = attribute;
  OpenPbrParameters openPbr;
  openPbrInput(openPbr, "base_color") = attribute;
  openPbrInput(openPbr, "specular_roughness") = sampler;
  UsdPreviewSurfaceParameters usd;
  usdPreviewSurfaceInput(usd, "diffuseColor") = attribute;
  usdPreviewSurfaceInput(usd, "roughness") = attribute;
  const std::pair<const char *, ConvertedMaterial> graphs[] = {
      {"matte", matteGraph(matte)},
      {"physicallyBased", physicallyBasedGraph(pbm)},
      {"openPBR", openPbrGraph(openPbr)},
      {"usdPreviewSurface", usdPreviewSurfaceGraph(usd)}};
  Attributes zero{};
  zero[3] = Vec4f(0.f, 0.f, 0.f, 1.f);
  Attributes other{};
  other[3] = Vec4f(0.6f, 0.5f, 0.4f, 0.5f);
  for (const auto &[name, converted] : graphs) {
    bool defaults = true;
    for (const auto &n : converted.graph.nodes) {
      if (n.second.nodeTypeId.rfind("ND_geompropvalue_", 0) != 0)
        continue;
      const auto d = n.second.parameters.find("default");
      defaults = defaults && d != n.second.parameters.end()
          && ValueHolds<Vec4f>(d->second)
          && ValueGet<Vec4f>(d->second) == Vec4f(0.f, 0.f, 0.f, 1.f);
    }
    auto cm = compile(converted);
    ok = defaults && cm && cm->attributeMask() == (1u << 3);
    if (ok) {
      const SurfaceClosure absent = evaluate(*cm, other, 0, ~(1u << 3));
      ok = sameClosure(absent, evaluate(*cm, zero))
          && !sameClosure(absent, evaluate(*cm, other));
    }
    check(ok,
        std::string(name)
            + ": an attribute the point lacks reads (0, 0, 0, 1)");
  }
}

// A glTF PBR surface whose base colour is the 'out' of node '/n' of type
// 'type' with 'parameters'; 'connected' inputs of '/n' are connected to a
// constant node
MaterialGraph baseColorFrom(const std::string &type,
    std::map<std::string, Value> parameters = {},
    std::vector<std::string> connected = {})
{
  MaterialGraph g;
  auto &n = g.nodes["/n"];
  n.nodeTypeId = type;
  n.parameters = std::move(parameters);
  for (const std::string &input : connected) {
    n.inputConnections[input] = {{"/constant", "out"}};
    auto &c = g.nodes["/constant"];
    c.nodeTypeId = "ND_constant_vector3";
    c.parameters["value"] = Value(Vec3f(1.f, 0.f, 0.f));
  }
  auto &surface = g.nodes["/surface"];
  surface.nodeTypeId = "ND_gltf_pbr_surfaceshader";
  surface.inputConnections["base_color"] = {{"/n", "out"}};
  g.terminals["surface"] = {"/surface", "out"};
  return g;
}

void testContextGroups()
{
  constexpr uint32_t DISPLAY_COLOR = ispc::PATH_CONTEXT_DISPLAY_COLOR;
  constexpr uint32_t PRIMITIVE = ispc::PATH_CONTEXT_PRIMITIVE;
  constexpr uint32_t SURFACE = ispc::PATH_CONTEXT_SURFACE_DERIVATIVES;
  constexpr uint32_t SCREEN = ispc::PATH_CONTEXT_SCREEN_DERIVATIVES;
  constexpr uint32_t TRANSFORMS = ispc::PATH_CONTEXT_TRANSFORMS;
  constexpr uint32_t OBJECT_POSITION = ispc::PATH_CONTEXT_OBJECT_POSITION;

  const auto mask = [](const MaterialGraph &g) {
    auto m = compile(g);
    return m ? int64_t(m->contextMask()) : int64_t(-1);
  };
  const auto expect =
      [&](const MaterialGraph &g, uint32_t expected, const std::string &name) {
        const int64_t got = mask(g);
        if (got != int64_t(expected))
          std::printf("  %s: 0x%llx, expected 0x%x\n",
              name.c_str(),
              (long long)got,
              expected);
        check(got == int64_t(expected), "context groups: " + name);
      };

  // the converter's graphs
  IspcSampler map;
  map.setMapping(ispc::ATTRIBUTE_0);
  map.setImage2D(TEXELS, 2, 2);
  expect(matteGraph(MatteParameters{}).graph, 0, "matte reads none");
  MatteParameters matte;
  matte.color = MaterialParameter::fromAttribute("color", Vec4f(1.f));
  expect(matteGraph(matte).graph, 0, "matte with an attribute reads none");
  PhysicallyBasedParameters pbm = distinctivePbm();
  expect(physicallyBasedGraph(pbm).graph, 0, "physicallyBased reads none");
  pbm.baseColor = MaterialParameter::fromSampler(map.get(), Vec4f(1.f));
  expect(physicallyBasedGraph(pbm).graph,
      PRIMITIVE,
      "a sampler reads the primitive (primitive samplers)");
  pbm = distinctivePbm();
  pbm.normal = map.get();
  expect(physicallyBasedGraph(pbm).graph,
      PRIMITIVE | SURFACE,
      "a normal map reads dPdu (its default tangent)");

  // nodes that read each part
  expect(baseColorFrom("ND_geomcolor_color3"),
      DISPLAY_COLOR,
      "geomcolor reads the display colour");
  expect(baseColorFrom("ND_image_color3",
             {{"file", Value(std::string("anari:sampler/0"))}}),
      SURFACE | SCREEN,
      "an image node reads the derivatives");
  expect(baseColorFrom("ND_splitlr_color3"),
      SCREEN,
      "splitlr reads the screen-space derivatives");
  expect(baseColorFrom("ND_transformpoint_vector3",
             {{"fromspace", Value(std::string("object"))},
                 {"tospace", Value(std::string("world"))}}),
      TRANSFORMS,
      "transformpoint reads the transforms");
  expect(baseColorFrom("ND_facingratio_float"),
      TRANSFORMS,
      "facingratio reads the transforms (the world position)");
  expect(baseColorFrom("ND_triplanarprojection_color3"),
      SCREEN | TRANSFORMS | OBJECT_POSITION,
      "triplanar projection reads the derivatives and transforms (and the"
      " object position through the position node Compile() adds)");
  expect(baseColorFrom("ND_noise3d_color3"),
      OBJECT_POSITION,
      "noise3d reads the object position (the position node Compile() adds)");
  expect(baseColorFrom("ND_noise3d_color3", {}, {"position"}),
      0,
      "noise3d of a connected position reads none");
  expect(baseColorFrom("ND_blur_color3"), 0, "blur reads none");
  expect(baseColorFrom("ND_constant_color3"), 0, "a constant reads none");

  // geometric nodes transform only into another space
  expect(baseColorFrom(
             "ND_position_vector3", {{"space", Value(std::string("world"))}}),
      TRANSFORMS,
      "a world position reads the transforms");
  expect(baseColorFrom(
             "ND_position_vector3", {{"space", Value(std::string("Pobject"))}}),
      OBJECT_POSITION,
      "an object position reads the object position from the mesh");
  expect(baseColorFrom("ND_position_vector3"),
      OBJECT_POSITION,
      "a position reads the object position (object space by default)");
  {
    // Typhoon's rule: only position nodes the surface reaches count
    MaterialGraph g = baseColorFrom("ND_constant_color3");
    g.nodes["/unused"].nodeTypeId = "ND_position_vector3";
    expect(g, 0, "an unconnected position node reads none");
  }
  expect(baseColorFrom("ND_normal_vector3"),
      TRANSFORMS,
      "a normal reads the transforms (object space by default)");
  expect(baseColorFrom(
             "ND_normal_vector3", {{"space", Value(std::string("world"))}}),
      0,
      "a world normal reads none");
  expect(baseColorFrom("ND_normal_vector3",
             {{"space", Value(std::string("world"))}},
             {"space"}),
      TRANSFORMS,
      "a connected space reads the transforms");
  expect(baseColorFrom(
             "ND_tangent_vector3", {{"space", Value(std::string("world"))}}),
      SURFACE,
      "a world tangent reads dPdu (its fallback)");

  // BSDF nodes read dPdu through the tangent node Compile() adds
  const auto bsdf = [](const char *type, std::vector<std::string> connected) {
    MaterialGraph g;
    auto &b = g.nodes["/bsdf"];
    b.nodeTypeId = type;
    for (const std::string &input : connected)
      b.inputConnections[input] = {{"/tangent", "out"}};
    auto &t = g.nodes["/tangent"];
    t.nodeTypeId = "ND_constant_vector3";
    t.parameters["value"] = Value(Vec3f(1.f, 0.f, 0.f));
    auto &s = g.nodes["/surface"];
    s.nodeTypeId = "ND_surface";
    s.inputConnections["bsdf"] = {{"/bsdf", "out"}};
    g.terminals["surface"] = {"/surface", "out"};
    return g;
  };
  expect(bsdf("ND_dielectric_bsdf", {}),
      SURFACE,
      "a dielectric BSDF reads dPdu (the tangent Compile() adds)");
  expect(bsdf("ND_dielectric_bsdf", {"tangent"}),
      0,
      "a dielectric BSDF with a tangent reads none");
  expect(
      bsdf("ND_oren_nayar_diffuse_bsdf", {}), 0, "a diffuse BSDF reads none");

  // the table was audited against the vendored sources
  std::ifstream provenance(HALCYON_TYPHOON_PROVENANCE);
  std::stringstream text;
  text << provenance.rdbuf();
  check(provenance
          && text.str().find(
                 std::string("Commit: `") + CONTEXT_GROUPS_AUDITED_COMMIT + "`")
              != std::string::npos,
      "the context groups were audited at the vendored Typhoon commit"
      " (re-audit ContextGroups.cpp when the pin moves)");
}

} // namespace

int main()
{
  testAttributeNames();
  testMatte();
  testMatteAlpha();
  testPhysicallyBased();
  testPhysicallyBasedAlpha();
  testTextureSystem();
  testSamplerParameters();
  testImageNode();
  testNormalMaps();
  testOpenPbr();
  testUsdPreviewSurface();
  testGeomPropBindings();
  testGeomPropPresence();
  testContextGroups();
  return g_failures ? 1 : 0;
}
