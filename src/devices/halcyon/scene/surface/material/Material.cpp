// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Material_ispc.h"
#include "renderer/pathtracer/MaterialXDocument.h"
#include "renderer/pathtracer/PathMaterial.h"
// std
#include <atomic>
#include <filesystem>
#include <mutex>

namespace halcyon {

namespace {

using mxcpp::Vec4f;

static_assert(int(AlphaMode::OPAQUE) == int(pathtracer::AlphaMode::OPAQUE)
    && int(AlphaMode::MASK) == int(pathtracer::AlphaMode::MASK)
    && int(AlphaMode::BLEND) == int(pathtracer::AlphaMode::BLEND));

// The default of colour input 'name' of 'inputs', as float4
float4 defaultColor(
    const std::vector<pathtracer::ShaderInput> &inputs, const char *name)
{
  for (const auto &in : inputs) {
    if (std::string(in.name) == name) {
      const Vec4f &v = in.defaultValue;
      return float4(v[0], v[1], v[2], v[3]);
    }
  }
  return float4(1.f);
}

// matte's 'color' default, the specification's (helide's is white): what
// pathtracer::MatteParameters holds, for the graph and as float4
const Vec4f MATTE_GRAPH_COLOR = pathtracer::MatteParameters{}.color.value;
const float4 MATTE_COLOR = float4(MATTE_GRAPH_COLOR[0],
    MATTE_GRAPH_COLOR[1],
    MATTE_GRAPH_COLOR[2],
    MATTE_GRAPH_COLOR[3]);
const float4 OPEN_PBR_COLOR =
    defaultColor(pathtracer::openPbrInputs(), "base_color");
const float4 USD_PREVIEW_SURFACE_COLOR =
    defaultColor(pathtracer::usdPreviewSurfaceInputs(), "diffuseColor");

pathtracer::AlphaMode graphAlphaMode(AlphaMode mode)
{
  // the same order (helium's)
  return pathtracer::AlphaMode(int(mode));
}

} // namespace

// Material subtypes //////////////////////////////////////////////////////////

struct Matte : public Material
{
  Matte(HalcyonDeviceGlobalState *s) : Material(s)
  {
    m_color = MATTE_COLOR;
    // the graph of the defaults, for a material never committed (buildGraph()
    // is this subtype's by now)
    Material::finalize();
  }

  void commitParameters() override
  {
    Material::commitParameters();
    commitColorParameter("color", MATTE_COLOR);
    m_params.color = readGraphParameter("color", MATTE_GRAPH_COLOR);
    m_params.opacity = readGraphParameter("opacity", Vec4f(1.f));
    m_params.alphaMode = graphAlphaMode(m_alphaMode);
    m_params.alphaCutoff = m_alphaCutoff;
    m_nativeReadsAgree = nativeColorReadsAgree("color");
  }

 protected:
  bool buildGraph(pathtracer::ConvertedMaterial &graph,
      std::vector<std::string> &unsupported) const override
  {
    graph = pathtracer::matteGraph(m_params);
    unsupported = pathtracer::unsupportedSamplerParameters(m_params);
    return true;
  }

  // PathMatte.isph: the graph's Lambertian natively, from the colour and
  // opacity the other renderers read (m_color, m_opacity, ...)
  int nativeKind() const override
  {
    return m_nativeReadsAgree ? ispc::MATERIAL_KIND_MATTE
                              : ispc::MATERIAL_KIND_CPP;
  }

 private:
  pathtracer::MatteParameters m_params;
  bool m_nativeReadsAgree{true};
};

struct PhysicallyBased : public Material
{
  PhysicallyBased(HalcyonDeviceGlobalState *s) : Material(s)
  {
    // the graph of the defaults, for a material never committed (buildGraph()
    // is this subtype's by now)
    updateNativeParameters();
    Material::finalize();
  }

  void commitParameters() override
  {
    Material::commitParameters();
    commitColorParameter("baseColor");

    // every KHR_MATERIAL_PHYSICALLY_BASED parameter, with the defaults
    // PhysicallyBasedParameters holds
    auto &p = m_params;
    const pathtracer::PhysicallyBasedParameters defaults;
    for (const auto &in : pathtracer::physicallyBasedInputs())
      p.*in.member = readGraphParameter(in.name, (defaults.*in.member).value);
    p.normal = samplerOf("normal");
    p.clearcoatNormal = samplerOf("clearcoatNormal");
    p.occlusion = samplerOf("occlusion");
    p.alphaMode = graphAlphaMode(m_alphaMode);
    p.alphaCutoff = m_alphaCutoff;
    m_nativeReadsAgree = nativeColorReadsAgree("baseColor");
    updateNativeParameters();
  }

 protected:
  bool buildGraph(pathtracer::ConvertedMaterial &graph,
      std::vector<std::string> &unsupported) const override
  {
    graph = pathtracer::physicallyBasedGraph(m_params);
    unsupported = pathtracer::unsupportedSamplerParameters(m_params);
    return true;
  }

  // PathPbm.isph: the graph's glTF PBR closure natively, for the parameters
  // it covers (pathtracer::nativePhysicallyBasedCovers()), from the colour
  // and opacity the other renderers read and the parameters below
  int nativeKind() const override
  {
    return m_nativeReadsAgree
            && pathtracer::nativePhysicallyBasedCovers(m_params)
        ? ispc::MATERIAL_KIND_PHYSICALLY_BASED
        : ispc::MATERIAL_KIND_CPP;
  }

  const void *physicallyBasedParameters() const override
  {
    return &m_native;
  }

 private:
  // m_native from m_params, the graph's inputs
  void updateNativeParameters()
  {
    const auto set = [](ispc::MaterialParameter &out,
                         const pathtracer::MaterialParameter &in) {
      using Source = pathtracer::MaterialParameter::Source;
      for (int i = 0; i < 4; i++)
        out.value.v[i] = in.value[i];
      out.attribute = in.source == Source::ATTRIBUTE
          ? ispc::Attribute(attributeFromString(in.attribute))
          : ispc::ATTRIBUTE_NONE;
      out.sampler = in.source == Source::SAMPLER
          ? static_cast<const ispc::Sampler *>(in.sampler)
          : nullptr;
    };
    const auto &p = m_params;
    set(m_native.metallic, p.metallic);
    set(m_native.roughness, p.roughness);
    set(m_native.specular, p.specular);
    set(m_native.specularColor, p.specularColor);
    set(m_native.ior, p.ior);
    set(m_native.emissive, p.emissive);
    set(m_native.clearcoat, p.clearcoat);
    set(m_native.clearcoatRoughness, p.clearcoatRoughness);
    set(m_native.sheenColor, p.sheenColor);
    set(m_native.sheenRoughness, p.sheenRoughness);
    set(m_native.transmission, p.transmission);
    set(m_native.thickness, p.thickness);
    m_native.normal = static_cast<const ispc::Sampler *>(p.normal);
    m_native.clearcoatNormal =
        static_cast<const ispc::Sampler *>(p.clearcoatNormal);
  }

  pathtracer::PhysicallyBasedParameters m_params;
  bool m_nativeReadsAgree{true};
  // What MATERIAL_KIND_PHYSICALLY_BASED reads (Material::physicallyBased
  // points here)
  ispc::PhysicallyBasedParameters m_native{};
};

// The other renderers see the MaterialX models (openPBR, usdPreviewSurface)
// as their base colour and opacity: the colour is the model's diffuse colour
// (a color3, so its alpha isn't opacity), and the opacity blends, opaque
// while it is the constant 1.
struct OpenPbr : public Material
{
  OpenPbr(HalcyonDeviceGlobalState *s) : Material(s)
  {
    m_colorAlphaIsOpacity = false;
    m_color = OPEN_PBR_COLOR;
    // the graph of the defaults, for a material never committed (buildGraph()
    // is this subtype's by now)
    Material::finalize();
  }

  void commitParameters() override
  {
    Material::commitParameters();
    commitColorParameter("base_color", OPEN_PBR_COLOR);
    commitOpacityParameter("geometry_opacity");
    m_alphaMode = opacityIsOne() ? AlphaMode::OPAQUE : AlphaMode::BLEND;
    m_alphaCutoff = 0.5f;

    auto &p = m_params;
    readShaderInputs(pathtracer::openPbrInputs(), p.inputs);
    p.thinWalled = getParam<bool>("geometry_thin_walled", false);
    // world-space vectors; an unknown attribute or (in buildGraph()) an
    // invalid sampler counts as unset, so the input keeps the nodedef's
    // Nworld or Tworld, as an invalid input gives the parameter's default
    p.normal = readGraphInputParameter("geometry_normal");
    p.coatNormal = readGraphInputParameter("geometry_coat_normal");
    p.tangent = readGraphInputParameter("geometry_tangent");
    p.coatTangent = readGraphInputParameter("geometry_coat_tangent");
  }

 protected:
  bool buildGraph(pathtracer::ConvertedMaterial &graph,
      std::vector<std::string> &) const override
  {
    pathtracer::OpenPbrParameters p = m_params;
    dropInvalidSampler(p.normal);
    dropInvalidSampler(p.coatNormal);
    dropInvalidSampler(p.tangent);
    dropInvalidSampler(p.coatTangent);
    graph = pathtracer::openPbrGraph(p);
    return true;
  }

 private:
  pathtracer::OpenPbrParameters m_params;
};

struct UsdPreviewSurface : public Material
{
  UsdPreviewSurface(HalcyonDeviceGlobalState *s) : Material(s)
  {
    m_colorAlphaIsOpacity = false;
    m_color = USD_PREVIEW_SURFACE_COLOR;
    Material::finalize();
  }

  void commitParameters() override
  {
    Material::commitParameters();
    commitColorParameter("diffuseColor", USD_PREVIEW_SURFACE_COLOR);
    // 'opacity' as the base reads it; a threshold makes it a mask, as in
    // the model
    auto &p = m_params;
    p.opacityThreshold = getParam<float>("opacityThreshold", 0.f);
    if (opacityIsOne())
      m_alphaMode = AlphaMode::OPAQUE;
    else if (p.opacityThreshold > 0.f)
      m_alphaMode = AlphaMode::MASK;
    else
      m_alphaMode = AlphaMode::BLEND;
    m_alphaCutoff = p.opacityThreshold;

    readShaderInputs(pathtracer::usdPreviewSurfaceInputs(), p.inputs);
    p.useSpecularWorkflow = getParam<int32_t>("useSpecularWorkflow", 0) != 0
        || getParam<bool>("useSpecularWorkflow", false);
    // opacityMode: the nodedef's integer (0, 1) or its enum names
    p.opacityMode = pathtracer::UsdOpacityMode::TRANSPARENT;
    std::string mode = getParamString("opacityMode", "transparent");
    if (int32_t i = 0; getParam("opacityMode", ANARI_INT32, &i))
      mode = i == 0 ? "transparent" : i == 1 ? "presence" : std::to_string(i);
    if (mode == "presence") {
      p.opacityMode = pathtracer::UsdOpacityMode::PRESENCE;
    } else if (mode != "transparent") {
      reportMessage(ANARI_SEVERITY_WARNING,
          "halcyon::Material: unknown usdPreviewSurface opacityMode '%s',"
          " using 'transparent'",
          mode.c_str());
    }
    // not read (Typhoon's evaluation ignores them): say so in DEBUG
    m_unused.clear();
    for (const char *name : {"displacement", "occlusion"}) {
      if (hasParam(name))
        m_unused.push_back(name);
    }
    p.normal = readOptionalGraphParameter("normal", Vec4f(0.f, 0.f, 1.f, 1.f));
  }

 protected:
  bool buildGraph(pathtracer::ConvertedMaterial &graph,
      std::vector<std::string> &unused) const override
  {
    graph = pathtracer::usdPreviewSurfaceGraph(m_params);
    unused = m_unused;
    return true;
  }

 private:
  pathtracer::UsdPreviewSurfaceParameters m_params;
  std::vector<std::string> m_unused;
};

// materialx (HALCYON_MATERIAL_MATERIALX: VisRTX's contract, with its
// parameter names): a material of a MaterialX document, converted into a
// MaterialXCpp graph (renderer/pathtracer/MaterialXDocument.h,
// docs/adr/0006-materialx-documents.md). Every parameter other than
// sourceType, source and materialName (and helium's 'name') overrides the
// shader input or texture input of its name. A document that doesn't read,
// a material it doesn't have and a graph that doesn't compile are ERRORs and
// leave the material invalid, so surfaces show it in the device's
// invalidMaterialColor in every renderer.
//
// The other renderers see the surface shader as its diffuse colour and
// opacity, as they see openPBR and usdPreviewSurface (the model's own input
// of that name: an override, else the document's constant, else the
// nodedef's default).
struct MaterialXSubtype : public Material
{
  MaterialXSubtype(HalcyonDeviceGlobalState *s) : Material(s)
  {
    m_colorAlphaIsOpacity = false;
  }

  void commitParameters() override
  {
    Material::commitParameters();

    // re-read the document only when what selects it changed
    std::string sourceType = getParamString("sourceType", "documentFile");
    if (sourceType != "documentFile" && sourceType != "documentInline") {
      reportMessage(ANARI_SEVERITY_WARNING,
          "halcyon::Material (materialx): unknown sourceType '%s', using"
          " 'documentFile'",
          sourceType.c_str());
      sourceType = "documentFile";
    }
    const std::string source = getParamString("source", "");
    const std::string materialName = getParamString("materialName", "");
    std::filesystem::file_time_type modified{};
    if (sourceType == "documentFile") {
      std::error_code ec;
      modified = std::filesystem::last_write_time(source, ec);
    }
    if (!m_read || sourceType != m_sourceType || source != m_source
        || materialName != m_materialName || modified != m_modified) {
      m_sourceType = sourceType;
      m_source = source;
      m_materialName = materialName;
      m_modified = modified;
      m_read = true;
      pathtracer::MaterialXMaterial doc = pathtracer::readMaterialX(
          sourceType == "documentInline"
              ? pathtracer::MaterialXSourceType::DOCUMENT_INLINE
              : pathtracer::MaterialXSourceType::DOCUMENT_FILE,
          source,
          materialName);
      report(doc.diagnostics);
      std::lock_guard<std::mutex> lock(m_mutex);
      m_doc = std::move(doc);
    }

    readOverrides();
    // report the overrides' problems once, here (finalize() applies them
    // again whenever a sampler changes)
    if (m_doc.valid) {
      std::vector<pathtracer::MaterialXDiagnostic> diagnostics;
      pathtracer::applyMaterialXOverrides(m_doc, m_overrides, diagnostics);
      report(diagnostics);
    }
    commitPreview();
  }

  void finalize() override
  {
    Material::finalize();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_valid = m_doc.valid && hasCompiledGraph();
    // the properties' lists, NULL-terminated
    m_textureInputs.clear();
    for (const auto &t : m_doc.textureInputs)
      m_textureInputs.push_back(t.first);
    const auto pointers = [](const std::vector<std::string> &strings,
                              std::vector<const char *> &out) {
      out.clear();
      for (const std::string &str : strings)
        out.push_back(str.c_str());
      out.push_back(nullptr);
    };
    pointers(m_doc.materialNames, m_materialNamePointers);
    pointers(m_textureInputs, m_textureInputPointers);
  }

  bool isValid() const override
  {
    return m_valid;
  }

  bool validityCanChange() const override
  {
    return true;
  }

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override
  {
    if (type == ANARI_STRING_LIST
        && (name == "materialNames" || name == "textureInputs")) {
      // only after a successful commit, as VisRTX; the lists stay valid
      // until the next commit
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!m_valid)
        return false;
      const auto &pointers = name == "materialNames"
          ? m_materialNamePointers
          : m_textureInputPointers;
      *static_cast<const char *const **>(ptr) = pointers.data();
      return true;
    }
    return Material::getProperty(name, type, ptr, size, flags);
  }

 protected:
  bool buildGraph(pathtracer::ConvertedMaterial &graph,
      std::vector<std::string> &) const override
  {
    if (!m_doc.valid)
      return false;
    // an invalid sampler counts as unset: the input keeps what the document
    // gives it (its value or connection, else the nodedef's default or
    // defaultgeomprop, Nworld for a normal)
    std::vector<pathtracer::MaterialXOverride> overrides;
    for (const auto &o : m_overrides) {
      if (o.kind != pathtracer::MaterialXOverride::Kind::SAMPLER
          || graphSamplerIsValid(o.sampler))
        overrides.push_back(o);
    }
    std::vector<pathtracer::MaterialXDiagnostic> diagnostics;
    graph = pathtracer::applyMaterialXOverrides(m_doc, overrides, diagnostics);
    return true;
  }

  void reportGraphFailure(const std::string &diagnostic) override
  {
    reportMessage(ANARI_SEVERITY_ERROR,
        "halcyon::Material (materialx): material '%s' doesn't compile (%s);"
        " it is invalid",
        m_doc.materialName.c_str(),
        diagnostic.c_str());
  }

 private:
  void report(const std::vector<pathtracer::MaterialXDiagnostic> &diagnostics)
  {
    using Severity = pathtracer::MaterialXDiagnostic::Severity;
    for (const auto &d : diagnostics) {
      const ANARIStatusSeverity severity = d.severity == Severity::ERROR
          ? ANARI_SEVERITY_ERROR
          : d.severity == Severity::WARNING ? ANARI_SEVERITY_WARNING
                                            : ANARI_SEVERITY_DEBUG;
      reportMessage(
          severity, "halcyon::Material (materialx): %s", d.message.c_str());
    }
  }

  // Every parameter but the document's selection, as an override
  void readOverrides()
  {
    using O = pathtracer::MaterialXOverride;
    m_overrides.clear();
    std::vector<std::string> names;
    for (auto p = params_begin(); p != params_end(); ++p)
      names.push_back(p->first);
    for (const std::string &name : names) {
      if (name == "name" || name == "sourceType" || name == "source"
          || name == "materialName" || !hasParam(name))
        continue;
      O o;
      o.name = name;
      const ANARIDataType type = getParamDirect(name).type();
      float4 v(0.f);
      switch (type) {
      case ANARI_SAMPLER:
        o.kind = O::Kind::SAMPLER;
        o.sampler = samplerOf(name.c_str());
        if (!o.sampler)
          continue;
        break;
      case ANARI_STRING:
        o.kind = O::Kind::STRING;
        o.string = getParamString(name, "");
        break;
      case ANARI_FLOAT32:
        o.value = mxcpp::Value(getParam<float>(name, 0.f));
        break;
      case ANARI_FLOAT32_VEC2:
        getParam(name, type, &v);
        o.value = mxcpp::Value(mxcpp::Vec2f(v.x, v.y));
        break;
      case ANARI_FLOAT32_VEC3:
        getParam(name, type, &v);
        o.value = mxcpp::Value(mxcpp::Vec3f(v.x, v.y, v.z));
        break;
      case ANARI_FLOAT32_VEC4:
        getParam(name, type, &v);
        o.value = mxcpp::Value(mxcpp::Vec4f(v.x, v.y, v.z, v.w));
        break;
      case ANARI_INT32:
        o.value = mxcpp::Value(int(getParam<int32_t>(name, 0)));
        break;
      case ANARI_BOOL:
        o.value = mxcpp::Value(getParam<bool>(name, false));
        break;
      default:
        reportMessage(ANARI_SEVERITY_WARNING,
            "halcyon::Material (materialx): parameter '%s' is a %s, which no"
            " input takes; it's ignored",
            name.c_str(),
            anari::toString(type));
        continue;
      }
      m_overrides.push_back(std::move(o));
    }
  }

  // The colour and opacity the other renderers show (see above)
  void commitPreview()
  {
    struct Model
    {
      const char *nodeDef;
      const char *color;
      const char *opacity; // a float, or nullptr
    };
    static const Model MODELS[] = {
        {"ND_standard_surface_surfaceshader", "base_color", nullptr},
        {"ND_open_pbr_surface_surfaceshader", "base_color", "geometry_opacity"},
        {"ND_gltf_pbr_surfaceshader", "base_color", "alpha"},
        {"ND_UsdPreviewSurface_surfaceshader", "diffuseColor", "opacity"},
        {"ND_disney_principled", "baseColor", nullptr}};
    const Model *model = nullptr;
    for (const Model &m : MODELS) {
      if (m_doc.valid && m_doc.shaderNodeDef == m.nodeDef)
        model = &m;
    }

    // the document's value of shader input 'input' (a constant, or the
    // nodedef's default where it's connected or unset), as float4
    const auto documentValue = [&](const char *input, const float4 &fallback) {
      const auto *node = m_doc.graph.nodes.count(m_doc.shaderNode)
          ? &m_doc.graph.nodes.at(m_doc.shaderNode)
          : nullptr;
      mxcpp::Value v;
      if (node && node->parameters.count(input)
          && !node->inputConnections.count(input))
        v = node->parameters.at(input);
      else if (m_doc.shaderInputs.count(input))
        v = m_doc.shaderInputs.at(input).defaultValue;
      if (mxcpp::ValueHolds<mxcpp::Vec3f>(v)) {
        const auto &c = mxcpp::ValueGet<mxcpp::Vec3f>(v);
        return float4(c[0], c[1], c[2], 1.f);
      }
      if (mxcpp::ValueHolds<float>(v))
        return float4(mxcpp::ValueGet<float>(v), 0.f, 0.f, 1.f);
      if (mxcpp::ValueHolds<int>(v))
        return float4(float(mxcpp::ValueGet<int>(v)), 0.f, 0.f, 1.f);
      return fallback;
    };

    m_color = float4(1.f);
    m_colorAttribute = Attribute::NONE;
    m_colorSampler = nullptr;
    m_opacity = 1.f;
    m_opacityAttribute = Attribute::NONE;
    m_opacitySampler = nullptr;
    m_alphaMode = AlphaMode::OPAQUE;
    m_alphaCutoff = 0.5f;
    if (!model)
      return;

    const float4 color = documentValue(model->color, float4(1.f));
    if (hasParam(model->color))
      commitColorParameter(model->color, color);
    else
      m_color = color;
    if (!model->opacity)
      return;
    if (hasParam(model->opacity))
      commitOpacityParameter(model->opacity);
    else
      m_opacity = documentValue(model->opacity, float4(1.f)).x;
    if (opacityIsOne())
      return;
    m_alphaMode = AlphaMode::BLEND;
    if (m_doc.shaderNodeDef == "ND_gltf_pbr_surfaceshader") {
      // alpha_mode: 0 opaque, 1 mask, 2 blend; alpha_cutoff
      int mode = int(documentValue("alpha_mode", float4(0.f)).x);
      mode = getParam<int32_t>("alpha_mode", mode);
      m_alphaMode = mode == 1 ? AlphaMode::MASK
          : mode == 2         ? AlphaMode::BLEND
                              : AlphaMode::OPAQUE;
      m_alphaCutoff = getParam<float>(
          "alpha_cutoff", documentValue("alpha_cutoff", float4(0.5f)).x);
    } else if (m_doc.shaderNodeDef == "ND_UsdPreviewSurface_surfaceshader") {
      const float threshold = getParam<float>("opacityThreshold",
          documentValue("opacityThreshold", float4(0.f)).x);
      if (threshold > 0.f) {
        m_alphaMode = AlphaMode::MASK;
        m_alphaCutoff = threshold;
      }
    }
  }

  // The selection the document was read for
  bool m_read{false};
  std::string m_sourceType;
  std::string m_source;
  std::string m_materialName;
  std::filesystem::file_time_type m_modified{};
  // Read in commitParameters(), only while no render runs; m_mutex guards it
  // against property queries from the app's threads
  pathtracer::MaterialXMaterial m_doc;
  std::vector<pathtracer::MaterialXOverride> m_overrides;
  std::atomic<bool> m_valid{false};
  std::mutex m_mutex;
  // The properties, as finalize() last saw the material
  std::vector<std::string> m_textureInputs;
  std::vector<const char *> m_materialNamePointers;
  std::vector<const char *> m_textureInputPointers;
};

// Base Material definitions //////////////////////////////////////////////////

Material::Material(HalcyonDeviceGlobalState *s)
    : Object(ANARI_MATERIAL, s, ispc::sizeof_Material())
{
  // helium only finalizes objects with committed parameters, so a material
  // that never gets any must already hold its defaults in ISPC (subtypes
  // finalize again for their graphs)
  Material::finalize();
}

Material::~Material() = default;

Material *Material::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  else if (subtype == "physicallyBased")
    return new PhysicallyBased(s);
  else if (subtype == "openPBR")
    return new OpenPbr(s);
  else if (subtype == "usdPreviewSurface")
    return new UsdPreviewSurface(s);
  else if (subtype == "materialx")
    return new MaterialXSubtype(s);
  else
    return (Material *)new UnknownObject(
        ANARI_MATERIAL, s, ispc::sizeof_Material());
}

void Material::commitParameters()
{
  m_graphSamplers.clear();
  commitOpacityParameter("opacity");
  m_alphaMode = alphaModeFromString(getParamString("alphaMode", "opaque"));
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
}

void Material::commitColorParameter(
    const char *name, const float4 &defaultColor)
{
  m_color = defaultColor;
  getParam(name, ANARI_FLOAT32_VEC3, &m_color);
  getParam(name, ANARI_FLOAT32_VEC4, &m_color);
  m_colorAttribute = attributeFromString(getParamString(name, "none"));
  m_colorSampler = getParamObject<Sampler>(name);
}

void Material::commitOpacityParameter(const char *name)
{
  m_opacity = getParam<float>(name, 1.f);
  m_opacityAttribute = attributeFromString(getParamString(name, "none"));
  m_opacitySampler = getParamObject<Sampler>(name);
}

bool Material::opacityIsOne() const
{
  return !m_opacitySampler && m_opacityAttribute == Attribute::NONE
      && m_opacity == 1.f;
}

void Material::readShaderInputs(
    const std::vector<pathtracer::ShaderInput> &inputs,
    std::vector<pathtracer::MaterialParameter> &values)
{
  values.clear();
  for (const auto &in : inputs)
    values.push_back(readGraphParameter(in.name, in.defaultValue));
}

const void *Material::samplerOf(const char *name)
{
  const pathtracer::MaterialParameter p = readGraphParameter(name, Vec4f(0.f));
  return p.source == pathtracer::MaterialParameter::Source::SAMPLER ? p.sampler
                                                                    : nullptr;
}

std::optional<pathtracer::MaterialParameter>
Material::readOptionalGraphParameter(
    const char *name, const mxcpp::Vec4f &defaultValue)
{
  if (!hasParam(name))
    return std::nullopt;
  return readGraphParameter(name, defaultValue);
}

std::optional<pathtracer::MaterialParameter> Material::readGraphInputParameter(
    const char *name)
{
  const std::string attribute = getParamString(name, "");
  if (!attribute.empty() && attributeFromString(attribute) == Attribute::NONE)
    return std::nullopt;
  return readOptionalGraphParameter(name, mxcpp::Vec4f(0.f, 0.f, 0.f, 1.f));
}

bool Material::graphSamplerIsValid(const void *ie) const
{
  for (const auto &sampler : m_graphSamplers) {
    if (sampler->getIE() == ie)
      return sampler->isValid();
  }
  return false;
}

void Material::dropInvalidSampler(
    std::optional<pathtracer::MaterialParameter> &p) const
{
  if (p && p->source == pathtracer::MaterialParameter::Source::SAMPLER
      && !graphSamplerIsValid(p->sampler))
    p.reset();
}

pathtracer::MaterialParameter Material::readGraphParameter(
    const char *name, const mxcpp::Vec4f &defaultValue)
{
  using P = pathtracer::MaterialParameter;
  if (Sampler *sampler = getParamObject<Sampler>(name)) {
    m_graphSamplers.emplace_back(this, sampler);
    return P::fromSampler(sampler->getIE(), defaultValue);
  }
  const std::string attribute = getParamString(name, "");
  if (!attribute.empty()) {
    return attributeFromString(attribute) != Attribute::NONE
        ? P::fromAttribute(attribute, defaultValue)
        : P::constant(defaultValue);
  }
  float4 v(defaultValue[0], defaultValue[1], defaultValue[2], defaultValue[3]);
  if (!getParam(name, ANARI_FLOAT32_VEC4, &v)
      && !getParam(name, ANARI_FLOAT32_VEC3, &v))
    getParam(name, ANARI_FLOAT32, &v.x);
  return P::constant(mxcpp::Vec4f(v.x, v.y, v.z, v.w));
}

bool Material::buildGraph(
    pathtracer::ConvertedMaterial &, std::vector<std::string> &) const
{
  return false;
}

bool Material::nativeColorReadsAgree(const char *colorName) const
{
  float f;
  float4 v;
  return !getParam(colorName, ANARI_FLOAT32, &f)
      && !getParam("opacity", ANARI_FLOAT32_VEC3, &v)
      && !getParam("opacity", ANARI_FLOAT32_VEC4, &v);
}

void Material::reportGraphFailure(const std::string &diagnostic)
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "halcyon::Material: the pathtracer's graph of this material doesn't"
      " compile (%s); the pathtracer shades it as a surface without a"
      " material",
      diagnostic.c_str());
}

bool Material::hasCompiledGraph() const
{
  return m_cppMaterial != nullptr;
}

bool Material::validityCanChange() const
{
  return false;
}

int Material::nativeKind() const
{
  return ispc::MATERIAL_KIND_CPP;
}

const void *Material::physicallyBasedParameters() const
{
  return nullptr;
}

void Material::finalize()
{
  // ISPC's Attribute and AlphaMode enums follow helium's order
  ispc::Material_setParameters(getIE(),
      &m_color.x,
      ispc::Attribute(m_colorAttribute),
      m_opacity,
      ispc::Attribute(m_opacityAttribute),
      ispc::AlphaMode(m_alphaMode),
      m_alphaCutoff,
      m_colorAlphaIsOpacity);
  // The pathtracer shades the subtype's MaterialXCpp graph in C++, or
  // natively where the subtype has a native kind; without a graph (or if it
  // doesn't compile) it lights the surface as one without a material, in the
  // device's invalidMaterialColor
  // commitParameters() already dropped the previous graph's samplers; its C++
  // material, which names them, is replaced below in the same flush, before
  // any render can read it
  std::unique_ptr<pathtracer::CppMaterial> cpp;
  pathtracer::ConvertedMaterial graph;
  std::vector<std::string> unsupported;
  if (buildGraph(graph, unsupported)) {
    // the image files its image nodes read, shared through the device
    pathtracer::ImageFiles images;
    for (const std::string &path : pathtracer::imageFiles(graph.graph)) {
      std::string error;
      auto image = deviceState()->imageFiles.load(path, &error);
      if (!image && !error.empty()) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "halcyon::Material: image file '%s' doesn't read (%s); textures"
            " of it give their default value",
            path.c_str(),
            error.c_str());
      }
      images[path] = std::move(image);
    }
    std::string diagnostic;
    cpp = pathtracer::CppMaterial::compile(graph.graph,
        std::move(graph.samplers),
        &diagnostic,
        std::move(images));
    if (!cpp) {
      reportGraphFailure(diagnostic);
    } else if (!diagnostic.empty()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "halcyon::Material: the pathtracer's graph of this material: %s",
          diagnostic.c_str());
    }
    for (const std::string &name : unsupported) {
      reportMessage(ANARI_SEVERITY_DEBUG,
          "halcyon::Material: the pathtracer doesn't use '%s'",
          name.c_str());
    }
  }
  const ispc::MaterialKind kind =
      cpp ? ispc::MaterialKind(nativeKind()) : ispc::MATERIAL_KIND_NONE;
  ispc::Material_setKind(getIE(),
      kind,
      cpp.get(),
      cpp ? cpp->attributeMask() : 0u,
      cpp ? cpp->contextMask() : 0u,
      cpp && (cpp->contextMask() & ispc::PATH_CONTEXT_OBJECT_POSITION));
  // which way the pathtracer shades it (native_materials checks this)
  if (kind == ispc::MATERIAL_KIND_MATTE) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::Material: the pathtracer shades this material natively"
        " (matte)");
  } else if (kind == ispc::MATERIAL_KIND_PHYSICALLY_BASED) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::Material: the pathtracer shades this material natively"
        " (physicallyBased)");
  } else if (kind == ispc::MATERIAL_KIND_CPP) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::Material: the pathtracer shades this material through its"
        " MaterialXCpp graph in C++");
  }
  ispc::Material_setPhysicallyBased(getIE(),
      kind == ispc::MATERIAL_KIND_PHYSICALLY_BASED
          ? static_cast<const ispc::PhysicallyBasedParameters *>(
                physicallyBasedParameters())
          : nullptr);
  m_cppMaterial = std::move(cpp);
  // Samplers are always passed on, valid or not: shading checks (a sampler's
  // ISPC hook is NULL while it is invalid)
  ispc::Material_setSamplers(getIE(),
      m_colorSampler ? m_colorSampler->getIE() : nullptr,
      m_opacitySampler ? m_opacitySampler->getIE() : nullptr);
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Material *);
