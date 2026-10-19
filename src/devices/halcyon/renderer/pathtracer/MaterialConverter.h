// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ANARI materials as MaterialXCpp graphs (mxcpp::MaterialGraph), which the
// pathtracer compiles into C++ materials (CppMaterial, PathMaterial.h): its
// counterpart of Typhoon's mxcppAdapter.cpp, which converts Hydra material
// networks. It takes the parameters as plain values, so it doesn't depend on
// helium; scene/surface/material/Material.cpp reads them.
//
// - matte: a Lambertian, MaterialX's ND_oren_nayar_diffuse_bsdf at roughness
//   0 under ND_surface, whose opacity is the material's after 'alphaMode'.
// - physicallyBased: one ND_gltf_pbr_surfaceshader, every KHR parameter on
//   its glTF input.
// - openPBR (HALCYON_MATERIAL_OPENPBR): one
//   ND_open_pbr_surface_surfaceshader, every input set from the parameter of
//   its name, with the nodedef's defaults.
// - usdPreviewSurface (HALCYON_MATERIAL_USD_PREVIEW_SURFACE): one
//   ND_UsdPreviewSurface_surfaceshader, likewise.
//
// A parameter is a constant (a node parameter), an attribute (an
// ND_geompropvalue_vector4 node whose geomprop is the attribute's name,
// which the pathtracer resolves at the hit like any attribute, and whose
// default, what a hit without the attribute reads, is (0, 0, 0, 1), as in
// the other renderers; then the component(s) the input takes) or a sampler
// (a sampler node, HalcyonTextureSystem.h, whose 'in' is the sampler's
// inAttribute through such a geomprop node, then the component(s)). The
// samplers-only 'normal' and 'clearcoatNormal' are tangent-space normal maps
// (an ND_normalmap_float); 'occlusion' is not used (glTF PBR has no input for
// it, and the specification applies it to ambient light only).

// typhoon
#include <renderer/materials/MaterialXCpp/graphTypes.h>
#include <renderer/materials/MaterialXCpp/mathTypes.h>
// std
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace halcyon::pathtracer {

// Where one material parameter comes from
struct MaterialParameter
{
  enum class Source
  {
    CONSTANT,
    ATTRIBUTE,
    SAMPLER
  };

  Source source{Source::CONSTANT};
  // The constant, and the parameter's default for the other sources: a float
  // in x, a colour in xyz with its alpha in w
  mxcpp::Vec4f value{0.f, 0.f, 0.f, 1.f};
  // ATTRIBUTE: the attribute's name ("attribute0"..."attribute3", "color",
  // "worldPosition", "worldNormal", "objectPosition", "objectNormal")
  std::string attribute;
  // SAMPLER: the sampler's ISPC object, which the material keeps alive; the
  // value is what the parameter takes while the sampler is invalid
  const void *sampler{nullptr};

  static MaterialParameter constant(float v);
  static MaterialParameter constant(const mxcpp::Vec3f &v);
  static MaterialParameter constant(const mxcpp::Vec4f &v);
  // 'value' is the default
  static MaterialParameter fromAttribute(
      const std::string &name, const mxcpp::Vec4f &value);
  static MaterialParameter fromSampler(
      const void *sampler, const mxcpp::Vec4f &value);
};

// helium::AlphaMode's order
enum class AlphaMode
{
  OPAQUE = 0,
  MASK,
  BLEND
};

// KHR_MATERIAL_MATTE, with the specification's defaults (matte's colour is
// grey 0.8; helide's is white)
struct MatteParameters
{
  MaterialParameter color =
      MaterialParameter::constant(mxcpp::Vec4f(0.8f, 0.8f, 0.8f, 1.f));
  MaterialParameter opacity = MaterialParameter::constant(1.f);
  AlphaMode alphaMode{AlphaMode::OPAQUE};
  float alphaCutoff{0.5f};
};

// KHR_MATERIAL_PHYSICALLY_BASED, with the specification's defaults
struct PhysicallyBasedParameters
{
  MaterialParameter baseColor =
      MaterialParameter::constant(mxcpp::Vec4f(1.f));
  MaterialParameter opacity = MaterialParameter::constant(1.f);
  MaterialParameter metallic = MaterialParameter::constant(1.f);
  MaterialParameter roughness = MaterialParameter::constant(1.f);
  MaterialParameter emissive =
      MaterialParameter::constant(mxcpp::Vec3f(0.f));
  MaterialParameter specular = MaterialParameter::constant(0.f);
  MaterialParameter specularColor =
      MaterialParameter::constant(mxcpp::Vec3f(1.f));
  MaterialParameter clearcoat = MaterialParameter::constant(0.f);
  MaterialParameter clearcoatRoughness = MaterialParameter::constant(0.f);
  MaterialParameter transmission = MaterialParameter::constant(0.f);
  MaterialParameter ior = MaterialParameter::constant(1.5f);
  MaterialParameter thickness = MaterialParameter::constant(0.f);
  MaterialParameter attenuationDistance =
      MaterialParameter::constant(INFINITY);
  MaterialParameter attenuationColor =
      MaterialParameter::constant(mxcpp::Vec3f(1.f));
  MaterialParameter sheenColor =
      MaterialParameter::constant(mxcpp::Vec3f(0.f));
  MaterialParameter sheenRoughness = MaterialParameter::constant(0.f);
  MaterialParameter iridescence = MaterialParameter::constant(0.f);
  MaterialParameter iridescenceIor = MaterialParameter::constant(1.3f);
  MaterialParameter iridescenceThickness = MaterialParameter::constant(0.f);
  // Samplers only (their ISPC objects), NULL if unset
  const void *normal{nullptr};
  const void *clearcoatNormal{nullptr};
  const void *occlusion{nullptr};
  AlphaMode alphaMode{AlphaMode::OPAQUE};
  float alphaCutoff{0.5f};
};

// A physicallyBased parameter that can be a constant, attribute or sampler:
// its ANARI name, its member and the glTF PBR input it sets
struct PhysicallyBasedInput
{
  const char *name;
  MaterialParameter PhysicallyBasedParameters::*member;
  const char *gltfInput;
  bool color; // a colour (xyz), else a float (x)
};

// Every such parameter, in the specification's order: the one list the
// graph and Material.cpp's reading go by
const std::vector<PhysicallyBasedInput> &physicallyBasedInputs();

// A material model's input that can be a constant, attribute or sampler,
// named as the ANARI parameter that sets it: its MaterialX name and type, and
// its nodedef's default (a float in x, a colour in xyz)
struct ShaderInput
{
  enum class Type
  {
    FLOAT,
    COLOR3
  };

  const char *name;
  Type type;
  mxcpp::Vec4f defaultValue;
};

// One value per input of 'inputs', each its default
std::vector<MaterialParameter> shaderInputDefaults(
    const std::vector<ShaderInput> &inputs);

// HALCYON_MATERIAL_OPENPBR: ND_open_pbr_surface_surfaceshader (version 1.1)
// with MaterialX's defaults
const std::vector<ShaderInput> &openPbrInputs();

struct OpenPbrParameters
{
  // openPbrInputs()' values, in its order
  std::vector<MaterialParameter> inputs = shaderInputDefaults(openPbrInputs());
  bool thinWalled{false}; // geometry_thin_walled, constants only
  // geometry_normal and geometry_coat_normal: world-space vectors, as the
  // nodedef's (a sampler's value as given, no normal map); unset, the
  // unperturbed normal (the nodedef's Nworld)
  std::optional<MaterialParameter> normal;
  std::optional<MaterialParameter> coatNormal;
  // geometry_tangent and geometry_coat_tangent: world-space vectors; unset,
  // the shading context's tangent (the nodedef's Tworld)
  std::optional<MaterialParameter> tangent;
  std::optional<MaterialParameter> coatTangent;
};

// The value of input 'name' of 'params' (which must name an input)
const MaterialParameter &openPbrInput(
    const OpenPbrParameters &params, const char *name);
MaterialParameter &openPbrInput(OpenPbrParameters &params, const char *name);

// HALCYON_MATERIAL_USD_PREVIEW_SURFACE: ND_UsdPreviewSurface_surfaceshader
// (UsdPreviewSurface 2.6) with its defaults
const std::vector<ShaderInput> &usdPreviewSurfaceInputs();

enum class UsdOpacityMode
{
  TRANSPARENT = 0, // the nodedef's enum values
  PRESENCE
};

struct UsdPreviewSurfaceParameters
{
  // usdPreviewSurfaceInputs()' values, in its order
  std::vector<MaterialParameter> inputs =
      shaderInputDefaults(usdPreviewSurfaceInputs());
  // constants only
  bool useSpecularWorkflow{false};
  UsdOpacityMode opacityMode{UsdOpacityMode::TRANSPARENT};
  float opacityThreshold{0.f};
  // the tangent-space normal, in [-1, 1]; unset, the unperturbed normal
  std::optional<MaterialParameter> normal;
};

const MaterialParameter &usdPreviewSurfaceInput(
    const UsdPreviewSurfaceParameters &params, const char *name);
MaterialParameter &usdPreviewSurfaceInput(
    UsdPreviewSurfaceParameters &params, const char *name);

// A material's graph, one 'surface' terminal, and the samplers its sampler
// nodes name by index (HalcyonTextureSystem)
struct ConvertedMaterial
{
  mxcpp::MaterialGraph graph;
  std::vector<const void *> samplers;
};

// A sampler-sourced parameter's node reads the sampler's inAttribute as it
// is when the graph is built, so the material builds it again when one of its
// samplers changes
ConvertedMaterial matteGraph(const MatteParameters &params);
ConvertedMaterial physicallyBasedGraph(const PhysicallyBasedParameters &params);

// Whether native physicallyBased (PathPbm.isph, MATERIAL_KIND_PHYSICALLY_BASED)
// computes what physicallyBasedGraph(params) does: whenever the graph's
// closure has no thin film, which needs constants: 'iridescence' at most
// 1e-7, or 'iridescenceThickness' at most 1e-7. Every other parameter may be
// a constant, attribute or sampler (the closure is then a diffuse lobe mixed
// with a transmission under a dielectric GGX reflection, mixed with a GGX
// metal, under a sheen and a clearcoat where they are on, with or without
// normal maps). The colour and opacity must also be of types the other
// renderers read (Material.cpp checks that).
bool nativePhysicallyBasedCovers(const PhysicallyBasedParameters &params);
ConvertedMaterial openPbrGraph(const OpenPbrParameters &params);
ConvertedMaterial usdPreviewSurfaceGraph(
    const UsdPreviewSurfaceParameters &params);

// The type of a node input that a MaterialParameter sets (GraphEditor)
enum class InputType
{
  FLOAT, // x
  VECTOR2, // xy
  VECTOR3, // xyz
  COLOR3, // xyz
  VECTOR4, // xyzw
  COLOR4 // xyzw
};

// Edits a graph that came from elsewhere (a MaterialX document's,
// MaterialXDocument.h) the way the graphs above are built: connects node
// inputs to attributes and samplers through the same nodes, which it adds
// under "/Material/" (paths a document's node keys never take).
class GraphEditor
{
 public:
  explicit GraphEditor(mxcpp::MaterialGraph graph);
  ~GraphEditor();

  mxcpp::MaterialGraph &graph();
  // The index of 'sampler' among the graph's samplers, added if new: what
  // an "anari:sampler/<index>" key names
  int samplerIndex(const void *sampler);
  // Replaces input 'input' of node 'node' (a value or a connection) with
  // parameter 'p', taking the components 'type' needs; the nodes it adds are
  // named after 'name'
  void connect(const std::string &node,
      const std::string &input,
      const MaterialParameter &p,
      InputType type,
      const std::string &name);
  // The graph and the samplers it names
  ConvertedMaterial take();

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// The parameters a graph from above doesn't take from their samplers, to
// tell the app: physicallyBased's 'occlusion'
std::vector<std::string> unsupportedSamplerParameters(
    const MatteParameters &params);
std::vector<std::string> unsupportedSamplerParameters(
    const PhysicallyBasedParameters &params);

} // namespace halcyon::pathtracer
