// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MaterialConverter.h"
#include "HalcyonTextureSystem.h"
#include "PathMaterial.h"
// std
#include <algorithm>
#include <stdexcept>

namespace halcyon::pathtracer {

// MaterialParameter //////////////////////////////////////////////////////////

MaterialParameter MaterialParameter::constant(float v)
{
  return constant(mxcpp::Vec4f(v, 0.f, 0.f, 1.f));
}

MaterialParameter MaterialParameter::constant(const mxcpp::Vec3f &v)
{
  return constant(mxcpp::Vec4f(v[0], v[1], v[2], 1.f));
}

MaterialParameter MaterialParameter::constant(const mxcpp::Vec4f &v)
{
  MaterialParameter p;
  p.value = v;
  return p;
}

MaterialParameter MaterialParameter::fromAttribute(
    const std::string &name, const mxcpp::Vec4f &value)
{
  MaterialParameter p = constant(value);
  p.source = Source::ATTRIBUTE;
  p.attribute = name;
  return p;
}

MaterialParameter MaterialParameter::fromSampler(
    const void *sampler, const mxcpp::Vec4f &value)
{
  MaterialParameter p = constant(value);
  p.source = Source::SAMPLER;
  p.sampler = sampler;
  return p;
}

namespace {

using mxcpp::GraphConnection;
using mxcpp::GraphNode;
using mxcpp::MaterialGraph;
using mxcpp::Value;
using mxcpp::Vec3f;
using mxcpp::Vec4f;

// A node input's value: a constant or an upstream node's output
struct Input
{
  Value value;
  GraphConnection connection;

  bool connected() const
  {
    return !connection.upstreamNode.empty();
  }
};

Input constantInput(Value v)
{
  Input in;
  in.value = std::move(v);
  return in;
}

Input connectedInput(const std::string &node, const std::string &output = "out")
{
  Input in;
  in.connection = {node, output};
  return in;
}

// The value an attribute's components become in an input
enum class Components
{
  X, // float: the first component
  XYZ_COLOR3, // color3: the first three
  XYZ_VECTOR3, // vector3: the first three
  W, // float: the fourth (a colour's alpha)
  XY_VECTOR2, // vector2: the first two
  XYZW_VECTOR4, // vector4: all four
  XYZW_COLOR4 // color4: all four
};

Value constantOf(const Vec4f &v, Components c)
{
  switch (c) {
  case Components::X:
    return Value(v[0]);
  case Components::XYZ_COLOR3:
  case Components::XYZ_VECTOR3:
    return Value(Vec3f(v[0], v[1], v[2]));
  case Components::XY_VECTOR2:
    return Value(mxcpp::Vec2f(v[0], v[1]));
  case Components::XYZW_VECTOR4:
  case Components::XYZW_COLOR4:
    return Value(v);
  case Components::W:
  default:
    return Value(v[3]);
  }
}

class GraphBuilder
{
 public:
  // 'terminal' is the node the 'surface' terminal reads
  explicit GraphBuilder(const std::string &terminal)
  {
    m_graph.terminals["surface"] = {terminal, "out"};
  }

  // Adds to 'graph' (which has none of the nodes it adds, all under
  // "/Material/")
  explicit GraphBuilder(MaterialGraph graph) : m_graph(std::move(graph)) {}

  MaterialGraph &graph()
  {
    return m_graph;
  }

  // The index of 'sampler' among the samplers the graph names, added if new
  int samplerIndex(const void *sampler)
  {
    const auto it = std::find(m_samplers.begin(), m_samplers.end(), sampler);
    if (it != m_samplers.end())
      return int(it - m_samplers.begin());
    m_samplers.push_back(sampler);
    return int(m_samplers.size()) - 1;
  }

  GraphNode &node(const std::string &path, const std::string &type)
  {
    GraphNode &n = m_graph.nodes[path];
    n.nodeTypeId = type;
    return n;
  }

  void set(const std::string &path, const std::string &input, const Input &in)
  {
    GraphNode &n = m_graph.nodes[path];
    if (in.connected())
      n.inputConnections[input] = {in.connection};
    else
      n.parameters[input] = in.value;
  }

  // What parameter 'p' (named 'name' in node paths) gives an input taking
  // 'components'
  Input input(const MaterialParameter &p,
      const std::string &name,
      Components components)
  {
    switch (p.source) {
    case MaterialParameter::Source::ATTRIBUTE:
      return attributeInput(p.attribute, name, components);
    case MaterialParameter::Source::SAMPLER:
      return samplerInput(p, name, components);
    case MaterialParameter::Source::CONSTANT:
    default:
      return constantInput(constantOf(p.value, components));
    }
  }

  // a * b, folded when both are constants
  Input multiply(const Input &a, const Input &b, const std::string &name)
  {
    if (!a.connected() && !b.connected()) {
      return constantInput(
          Value(std::get<float>(a.value) * std::get<float>(b.value)));
    }
    const std::string path = "/Material/" + name;
    node(path, "ND_multiply_float");
    set(path, "in1", a);
    set(path, "in2", b);
    return connectedInput(path);
  }

  // The tangent-space normal map of sampler 'sampler' (values in [-1, 1],
  // as the specification has them) as a world-space normal: MaterialX's
  // normal map node, which takes them encoded in [0, 1], at scale 1 (the
  // specification puts glTF's scale in the sampler). The unperturbed normal
  // while the sampler is invalid.
  Input normalMap(const void *sampler, const std::string &name)
  {
    const std::string value = samplerNode(sampler, Vec4f(0.f, 0.f, 1.f, 1.f));
    const std::string prefix = "/Material/" + name + "/";
    node(prefix + "vector", "ND_convert_vector4_vector3");
    set(prefix + "vector", "in", connectedInput(value));
    node(prefix + "half", "ND_multiply_vector3FA").parameters["in2"] =
        Value(0.5f);
    set(prefix + "half", "in1", connectedInput(prefix + "vector"));
    node(prefix + "encoded", "ND_add_vector3FA").parameters["in2"] =
        Value(0.5f);
    set(prefix + "encoded", "in1", connectedInput(prefix + "half"));
    const std::string path = "/Material/" + name;
    node(path, "ND_normalmap_float").parameters["scale"] = Value(1.f);
    set(path, "in", connectedInput(prefix + "encoded"));
    return connectedInput(path);
  }

  // The shading normal in world space, unperturbed
  Input shadingNormal()
  {
    const std::string path = "/Material/shadingNormal";
    node(path, "ND_normal_vector3").parameters["space"] =
        Value(std::string("world"));
    return connectedInput(path);
  }

  // The shading context's tangent in world space
  Input worldTangent(const std::string &name)
  {
    const std::string path = "/Material/" + name;
    node(path, "ND_tangent_vector3").parameters["space"] =
        Value(std::string("world"));
    return connectedInput(path);
  }

  ConvertedMaterial take()
  {
    return {std::move(m_graph), std::move(m_samplers)};
  }

 private:
  // The ND_geompropvalue_vector4 node of 'attribute' (one per attribute),
  // which the pathtracer's geomprop lookup resolves at the hit; its default,
  // which a hit without the attribute reads, is
  // halcyon's (0, 0, 0, 1), like every renderer's attributes
  std::string attributeValue(const std::string &attribute)
  {
    const std::string path = "/Material/attribute/" + attribute;
    if (m_graph.nodes.find(path) == m_graph.nodes.end()) {
      GraphNode &n = node(path, "ND_geompropvalue_vector4");
      n.parameters["geomprop"] = Value(attribute);
      n.parameters["default"] = Value(Vec4f(0.f, 0.f, 0.f, 1.f));
    }
    return path;
  }

  // The components an input takes of vector4 node 'value''s output, as node
  // '/Material/<name>'
  Input components(
      const std::string &value, const std::string &name, Components c)
  {
    const std::string path = "/Material/" + name;
    switch (c) {
    case Components::XYZW_VECTOR4:
      return connectedInput(value);
    case Components::XYZ_COLOR3:
      node(path, "ND_convert_vector4_color3");
      break;
    case Components::XYZ_VECTOR3:
      node(path, "ND_convert_vector4_vector3");
      break;
    case Components::XY_VECTOR2:
      node(path, "ND_convert_vector4_vector2");
      break;
    case Components::XYZW_COLOR4:
      node(path, "ND_convert_vector4_color4");
      break;
    case Components::X:
    case Components::W:
      node(path, "ND_extract_vector4").parameters["index"] =
          Value(c == Components::X ? 0 : 3);
      break;
    }
    set(path, "in", connectedInput(value));
    return connectedInput(path);
  }

  Input attributeInput(
      const std::string &attribute, const std::string &name, Components c)
  {
    return components(attributeValue(attribute), name, c);
  }

  // The sampler node of 'sampler' giving 'value' while the sampler is invalid
  // (one per sampler and value), whose 'in' is the sampler's inAttribute as
  // it is now
  std::string samplerNode(const void *sampler, const Vec4f &value)
  {
    for (const SamplerNode &n : m_samplerNodes) {
      if (n.sampler == sampler && n.value == value)
        return n.path;
    }
    const int index = samplerIndex(sampler);

    const std::string path =
        "/Material/sampler/" + std::to_string(m_samplerNodes.size());
    GraphNode &n = node(path, SAMPLER_NODE_TYPE);
    n.parameters["sampler"] = Value(index);
    n.parameters["default"] = Value(value);
    if (const char *attribute = attributeName(samplerInputAttribute(sampler)))
      set(path, "in", connectedInput(attributeValue(attribute)));
    m_samplerNodes.push_back({sampler, value, path});
    return path;
  }

  Input samplerInput(
      const MaterialParameter &p, const std::string &name, Components c)
  {
    return components(samplerNode(p.sampler, p.value), name, c);
  }

  struct SamplerNode
  {
    const void *sampler;
    Vec4f value;
    std::string path;
  };

  MaterialGraph m_graph;
  std::vector<const void *> m_samplers;
  std::vector<SamplerNode> m_samplerNodes;
};

// The material's alpha before 'alphaMode': its colour's alpha times its
// opacity, as halcyon's other renderers (Surface_getOpacity())
Input alpha(GraphBuilder &b,
    const MaterialParameter &color,
    const MaterialParameter &opacity)
{
  return b.multiply(b.input(color, "alpha/color", Components::W),
      b.input(opacity, "alpha/opacity", Components::X),
      "alpha");
}

} // namespace

// matte //////////////////////////////////////////////////////////////////////

ConvertedMaterial matteGraph(const MatteParameters &params)
{
  // Typhoon has no matte material; the equivalent USD material is MaterialX's
  // Lambertian, an Oren-Nayar diffuse BSDF of roughness 0 in a surface
  const std::string surface = "/Material/surface";
  const std::string diffuse = "/Material/diffuse";
  GraphBuilder b(surface);

  GraphNode &d = b.node(diffuse, "ND_oren_nayar_diffuse_bsdf");
  d.parameters["weight"] = Value(1.f);
  d.parameters["roughness"] = Value(0.f);
  b.set(
      diffuse, "color", b.input(params.color, "color", Components::XYZ_COLOR3));

  b.node(surface, "ND_surface");
  b.set(surface, "bsdf", connectedInput(diffuse));

  // ND_surface's opacity is the closure's presence: the material's opacity
  // after 'alphaMode', as halcyon's adjustOpacityFromMode()
  switch (params.alphaMode) {
  case AlphaMode::OPAQUE:
    break;
  case AlphaMode::BLEND:
    b.set(surface, "opacity", alpha(b, params.color, params.opacity));
    break;
  case AlphaMode::MASK: {
    const Input a = alpha(b, params.color, params.opacity);
    if (!a.connected()) {
      const float clamped = std::clamp(std::get<float>(a.value), 0.f, 1.f);
      b.set(surface,
          "opacity",
          constantInput(Value(clamped >= params.alphaCutoff ? 1.f : 0.f)));
      break;
    }
    const std::string clamp = "/Material/alpha/clamp";
    b.node(clamp, "ND_clamp_float");
    b.set(clamp, "in", a);
    const std::string mask = "/Material/alpha/mask";
    GraphNode &m = b.node(mask, "ND_ifgreatereq_float");
    m.parameters["value2"] = Value(params.alphaCutoff);
    m.parameters["in1"] = Value(1.f);
    m.parameters["in2"] = Value(0.f);
    b.set(mask, "value1", connectedInput(clamp));
    b.set(surface, "opacity", connectedInput(mask));
    break;
  }
  }
  return b.take();
}

std::vector<std::string> unsupportedSamplerParameters(const MatteParameters &)
{
  return {};
}

// physicallyBased ////////////////////////////////////////////////////////////

const std::vector<PhysicallyBasedInput> &physicallyBasedInputs()
{
  using P = PhysicallyBasedParameters;
  static const std::vector<PhysicallyBasedInput> inputs = {
      {"baseColor", &P::baseColor, "base_color", true},
      {"opacity", &P::opacity, nullptr, false}, // into alpha, below
      {"metallic", &P::metallic, "metallic", false},
      {"roughness", &P::roughness, "roughness", false},
      {"emissive", &P::emissive, "emissive", true},
      {"specular", &P::specular, "specular", false},
      {"specularColor", &P::specularColor, "specular_color", true},
      {"clearcoat", &P::clearcoat, "clearcoat", false},
      {"clearcoatRoughness",
          &P::clearcoatRoughness,
          "clearcoat_roughness",
          false},
      {"transmission", &P::transmission, "transmission", false},
      {"ior", &P::ior, "ior", false},
      {"thickness", &P::thickness, "thickness", false},
      // MaterialX's glTF PBR node has these (KHR_materials_volume), but
      // Typhoon's evaluation of it doesn't read them
      {"attenuationDistance",
          &P::attenuationDistance,
          "attenuation_distance",
          false},
      {"attenuationColor", &P::attenuationColor, "attenuation_color", true},
      {"sheenColor", &P::sheenColor, "sheen_color", true},
      {"sheenRoughness", &P::sheenRoughness, "sheen_roughness", false},
      {"iridescence", &P::iridescence, "iridescence", false},
      {"iridescenceIor", &P::iridescenceIor, "iridescence_ior", false},
      {"iridescenceThickness",
          &P::iridescenceThickness,
          "iridescence_thickness",
          false}};
  return inputs;
}

ConvertedMaterial physicallyBasedGraph(const PhysicallyBasedParameters &params)
{
  // ANARI's physicallyBased follows glTF's metallic-roughness material and
  // its KHR extensions, which MaterialX's glTF PBR node implements; every
  // input is set, so ANARI's defaults (specular 0, not glTF's 1) hold
  const std::string surface = "/Material/gltf_pbr";
  GraphBuilder b(surface);
  b.node(surface, "ND_gltf_pbr_surfaceshader");

  for (const PhysicallyBasedInput &in : physicallyBasedInputs()) {
    if (!in.gltfInput)
      continue;
    b.set(surface,
        in.gltfInput,
        b.input(params.*in.member,
            in.gltfInput,
            in.color ? Components::XYZ_COLOR3 : Components::X));
  }

  // The samplers-only normal maps. glTF's clearcoat has no normal map unless
  // it has its own (its clearcoat_normal defaults to the node's normal).
  // glTF PBR has no input for 'occlusion'.
  if (params.normal)
    b.set(surface, "normal", b.normalMap(params.normal, "normal"));
  if (params.clearcoatNormal) {
    b.set(surface,
        "clearcoat_normal",
        b.normalMap(params.clearcoatNormal, "clearcoat_normal"));
  } else if (params.normal) {
    b.set(surface, "clearcoat_normal", b.shadingNormal());
  }

  // glTF's alpha modes are halcyon's: opaque ignores alpha, mask compares the
  // clamped alpha with the cutoff, blend uses it
  b.set(surface, "alpha_mode", constantInput(Value(int(params.alphaMode))));
  b.set(surface, "alpha_cutoff", constantInput(Value(params.alphaCutoff)));
  if (params.alphaMode != AlphaMode::OPAQUE)
    b.set(surface, "alpha", alpha(b, params.baseColor, params.opacity));
  return b.take();
}

bool nativePhysicallyBasedCovers(const PhysicallyBasedParameters &params)
{
  // EvalGltfPbr()'s thin film: HasThinFilm(clamp01(iridescence), thickness)
  const auto constantAtMost = [](const MaterialParameter &p, float limit) {
    return p.source == MaterialParameter::Source::CONSTANT
        && !(p.value[0] > limit);
  };
  MaterialParameter iridescence = params.iridescence;
  iridescence.value[0] = std::clamp(iridescence.value[0], 0.f, 1.f);
  return constantAtMost(iridescence, 1.0e-7f)
      || constantAtMost(params.iridescenceThickness, 1.0e-7f);
}

std::vector<std::string> unsupportedSamplerParameters(
    const PhysicallyBasedParameters &params)
{
  if (params.occlusion)
    return {"occlusion"};
  return {};
}

// Shader inputs //////////////////////////////////////////////////////////////

std::vector<MaterialParameter> shaderInputDefaults(
    const std::vector<ShaderInput> &inputs)
{
  std::vector<MaterialParameter> values;
  values.reserve(inputs.size());
  for (const ShaderInput &in : inputs)
    values.push_back(MaterialParameter::constant(in.defaultValue));
  return values;
}

namespace {

size_t shaderInputIndex(
    const std::vector<ShaderInput> &inputs, const char *name)
{
  for (size_t i = 0; i < inputs.size(); i++) {
    if (std::string(inputs[i].name) == name)
      return i;
  }
  throw std::invalid_argument(
      std::string("halcyon: no material model input named ") + name);
}

ShaderInput floatInput(const char *name, float v)
{
  return {name, ShaderInput::Type::FLOAT, Vec4f(v, 0.f, 0.f, 1.f)};
}

ShaderInput color3Input(const char *name, float r, float g, float b)
{
  return {name, ShaderInput::Type::COLOR3, Vec4f(r, g, b, 1.f)};
}

ShaderInput color3Input(const char *name, float v)
{
  return color3Input(name, v, v, v);
}

// Sets every input of 'inputs' on node 'path' from its value in 'values'
void setShaderInputs(GraphBuilder &b,
    const std::string &path,
    const std::vector<ShaderInput> &inputs,
    const std::vector<MaterialParameter> &values)
{
  for (size_t i = 0; i < inputs.size(); i++) {
    const ShaderInput &in = inputs[i];
    b.set(path,
        in.name,
        b.input(values.at(i),
            in.name,
            in.type == ShaderInput::Type::COLOR3 ? Components::XYZ_COLOR3
                                                 : Components::X));
  }
}

bool isConstant(const MaterialParameter &p, float v)
{
  return p.source == MaterialParameter::Source::CONSTANT && p.value[0] == v;
}

} // namespace

// openPBR ////////////////////////////////////////////////////////////////////

const std::vector<ShaderInput> &openPbrInputs()
{
  // MaterialX's open_pbr_surface.mtlx (OpenPBR 1.1), in its order; the
  // geometry_* vectors and geometry_thin_walled are OpenPbrParameters' own
  static const std::vector<ShaderInput> inputs = {
      floatInput("base_weight", 1.f),
      color3Input("base_color", 0.8f),
      floatInput("base_diffuse_roughness", 0.f),
      floatInput("base_metalness", 0.f),
      floatInput("specular_weight", 1.f),
      color3Input("specular_color", 1.f),
      floatInput("specular_roughness", 0.3f),
      floatInput("specular_ior", 1.5f),
      floatInput("specular_roughness_anisotropy", 0.f),
      floatInput("transmission_weight", 0.f),
      color3Input("transmission_color", 1.f),
      floatInput("transmission_depth", 0.f),
      color3Input("transmission_scatter", 0.f),
      floatInput("transmission_scatter_anisotropy", 0.f),
      floatInput("transmission_dispersion_scale", 0.f),
      floatInput("transmission_dispersion_abbe_number", 20.f),
      floatInput("subsurface_weight", 0.f),
      color3Input("subsurface_color", 0.8f),
      floatInput("subsurface_radius", 1.f),
      color3Input("subsurface_radius_scale", 1.f, 0.5f, 0.25f),
      floatInput("subsurface_scatter_anisotropy", 0.f),
      floatInput("fuzz_weight", 0.f),
      color3Input("fuzz_color", 1.f),
      floatInput("fuzz_roughness", 0.5f),
      floatInput("coat_weight", 0.f),
      color3Input("coat_color", 1.f),
      floatInput("coat_roughness", 0.f),
      floatInput("coat_roughness_anisotropy", 0.f),
      floatInput("coat_ior", 1.6f),
      floatInput("coat_darkening", 1.f),
      floatInput("thin_film_weight", 0.f),
      floatInput("thin_film_thickness", 0.5f),
      floatInput("thin_film_ior", 1.4f),
      floatInput("emission_luminance", 0.f),
      color3Input("emission_color", 1.f),
      floatInput("geometry_opacity", 1.f)};
  return inputs;
}

const MaterialParameter &openPbrInput(
    const OpenPbrParameters &params, const char *name)
{
  return params.inputs.at(shaderInputIndex(openPbrInputs(), name));
}

MaterialParameter &openPbrInput(OpenPbrParameters &params, const char *name)
{
  return params.inputs.at(shaderInputIndex(openPbrInputs(), name));
}

ConvertedMaterial openPbrGraph(const OpenPbrParameters &params)
{
  // Every input is set, so the nodedef's defaults hold where Typhoon's
  // evaluation would fall back to others
  const std::string surface = "/Material/open_pbr";
  GraphBuilder b(surface);
  b.node(surface, "ND_open_pbr_surface_surfaceshader")
      .parameters["geometry_thin_walled"] = Value(params.thinWalled);
  setShaderInputs(b, surface, openPbrInputs(), params.inputs);

  // Normals, world-space vectors as the nodedef's. The nodedef's coat
  // normal is Nworld: a base normal doesn't perturb the coat (Typhoon's
  // evaluation would give the coat the base's normal where the input is
  // unset).
  const auto worldVector = [&](const MaterialParameter &p, const char *input) {
    b.set(surface, input, b.input(p, input, Components::XYZ_VECTOR3));
  };
  if (params.normal)
    worldVector(*params.normal, "geometry_normal");
  if (params.coatNormal)
    worldVector(*params.coatNormal, "geometry_coat_normal");
  else if (params.normal)
    b.set(surface, "geometry_coat_normal", b.shadingNormal());

  // Tangents, which only anisotropic lobes read: the parameter, else the
  // nodedef's Tworld (where Typhoon's evaluation would take world x), which
  // is left out where the lobe is isotropic so that the material doesn't
  // read the surface derivatives a tangent node may fall back to
  const auto tangent = [&](const std::optional<MaterialParameter> &p,
                           const char *anisotropy,
                           const char *input) {
    if (p)
      worldVector(*p, input);
    else if (!isConstant(openPbrInput(params, anisotropy), 0.f))
      b.set(surface, input, b.worldTangent(input));
  };
  tangent(params.tangent, "specular_roughness_anisotropy", "geometry_tangent");
  tangent(
      params.coatTangent, "coat_roughness_anisotropy", "geometry_coat_tangent");
  return b.take();
}

// usdPreviewSurface //////////////////////////////////////////////////////////

const std::vector<ShaderInput> &usdPreviewSurfaceInputs()
{
  // MaterialX's usd_preview_surface.mtlx (UsdPreviewSurface 2.6), in its
  // order; the constants-only inputs and 'normal' are
  // UsdPreviewSurfaceParameters' own, and 'displacement' and 'occlusion'
  // aren't read (Typhoon's evaluation ignores them)
  static const std::vector<ShaderInput> inputs = {
      color3Input("diffuseColor", 0.18f),
      color3Input("emissiveColor", 0.f),
      color3Input("specularColor", 0.f),
      floatInput("metallic", 0.f),
      floatInput("roughness", 0.5f),
      floatInput("clearcoat", 0.f),
      floatInput("clearcoatRoughness", 0.01f),
      floatInput("opacity", 1.f),
      floatInput("ior", 1.5f)};
  return inputs;
}

const MaterialParameter &usdPreviewSurfaceInput(
    const UsdPreviewSurfaceParameters &params, const char *name)
{
  return params.inputs.at(shaderInputIndex(usdPreviewSurfaceInputs(), name));
}

MaterialParameter &usdPreviewSurfaceInput(
    UsdPreviewSurfaceParameters &params, const char *name)
{
  return params.inputs.at(shaderInputIndex(usdPreviewSurfaceInputs(), name));
}

ConvertedMaterial usdPreviewSurfaceGraph(
    const UsdPreviewSurfaceParameters &params)
{
  const std::string surface = "/Material/usd_preview_surface";
  GraphBuilder b(surface);
  GraphNode &n = b.node(surface, "ND_UsdPreviewSurface_surfaceshader");
  n.parameters["useSpecularWorkflow"] = Value(int(params.useSpecularWorkflow));
  n.parameters["opacityMode"] = Value(int(params.opacityMode));
  n.parameters["opacityThreshold"] = Value(params.opacityThreshold);
  setShaderInputs(b, surface, usdPreviewSurfaceInputs(), params.inputs);
  // The model takes 'normal' in tangent space only where it is set
  if (params.normal) {
    b.set(surface,
        "normal",
        b.input(*params.normal, "normal", Components::XYZ_VECTOR3));
  }
  return b.take();
}

// GraphEditor ////////////////////////////////////////////////////////////////

struct GraphEditor::Impl
{
  explicit Impl(MaterialGraph graph) : builder(std::move(graph)) {}

  GraphBuilder builder;
};

GraphEditor::GraphEditor(mxcpp::MaterialGraph graph)
    : m_impl(std::make_unique<Impl>(std::move(graph)))
{}

GraphEditor::~GraphEditor() = default;

mxcpp::MaterialGraph &GraphEditor::graph()
{
  return m_impl->builder.graph();
}

int GraphEditor::samplerIndex(const void *sampler)
{
  return m_impl->builder.samplerIndex(sampler);
}

void GraphEditor::connect(const std::string &node,
    const std::string &input,
    const MaterialParameter &p,
    InputType type,
    const std::string &name)
{
  Components c = Components::X;
  switch (type) {
  case InputType::FLOAT:
    c = Components::X;
    break;
  case InputType::VECTOR2:
    c = Components::XY_VECTOR2;
    break;
  case InputType::VECTOR3:
    c = Components::XYZ_VECTOR3;
    break;
  case InputType::COLOR3:
    c = Components::XYZ_COLOR3;
    break;
  case InputType::VECTOR4:
    c = Components::XYZW_VECTOR4;
    break;
  case InputType::COLOR4:
    c = Components::XYZW_COLOR4;
    break;
  }
  GraphBuilder &b = m_impl->builder;
  b.graph().nodes[node].inputConnections.erase(input);
  b.graph().nodes[node].parameters.erase(input);
  b.set(node, input, b.input(p, "input/" + name, c));
}

ConvertedMaterial GraphEditor::take()
{
  return m_impl->builder.take();
}

} // namespace halcyon::pathtracer
