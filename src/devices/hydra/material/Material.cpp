// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Material.h"
#include "Matte.h"
#include "PhysicallyBased.h"

namespace anari_hydra {

Material::Material(HydraGlobalState *s) : Object(ANARI_MATERIAL, s)
{
  m_serial = s->nextSerial++;
}

Material *Material::createInstance(
    std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  else if (subtype == "physicallyBased")
    return new PhysicallyBased(s);
  else
    return (Material *)new UnknownObject(ANARI_MATERIAL, s);
}

void Material::commitParameters()
{
  // Subtypes fill m_color/m_opacity/etc. and record bindings; clear the
  // previous binding sets first.
  m_samplerBindings.clear();
  m_attributeBindings.clear();
}

bool Material::bindSampler(const char *name,
    const pxr::TfToken &usdInput,
    const pxr::TfToken &output,
    bool normalMap)
{
  auto s = getParamObject<Sampler>(name);
  if (!s)
    return false;
  m_samplerBindings.push_back({usdInput, output, s, normalMap});
  return true;
}

bool Material::bindAttribute(const char *name, const pxr::TfToken &usdInput)
{
  // An attribute-bound input names an ANARI attribute as a STRING (SPEC §6.1).
  if (!hasParam(name, ANARI_STRING))
    return false;
  const std::string attr = getParamString(name, "");
  // Supported: `color` (per-vertex color) and generic `attribute0..3`. Every
  // other attribute (worldPosition, primitiveId, none, ...) has no per-geometry
  // primvar target in v1 and is treated as "none": no binding, warned.
  if (attr != "color" && attr != "attribute0" && attr != "attribute1"
      && attr != "attribute2" && attr != "attribute3") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "material input '%s' bound to unsupported attribute '%s'; treated as "
        "none (SPEC §6.1)",
        name, attr.c_str());
    return false;
  }
  // The reader's `varname` matches the per-vertex primvar PrimBuilder authors
  // for this attribute (color -> `color`, attributeN -> `attributeN`).
  m_attributeBindings.push_back({usdInput, attr});
  return true;
}

void Material::finalize()
{
  deviceState()->mirrorDirty.mark(this);
}

// Matte //////////////////////////////////////////////////////////////////////

Matte::Matte(HydraGlobalState *s) : Material(s) {}

void Matte::commitParameters()
{
  Material::commitParameters();
  // matte maps exactly: color->diffuseColor, opacity, metallic=0, roughness=1
  // (SPEC §6.5). Each slot accepts a constant, a sampler, or an attribute
  // (SPEC §6.1); sampler wins over attribute wins over the constant read.
  if (!bindSampler("color", pxr::TfToken("diffuseColor"), pxr::TfToken("rgb"))
      && !bindAttribute("color", pxr::TfToken("diffuseColor")))
    m_color = getParam<float3>("color", float3(0.8f, 0.8f, 0.8f));
  if (!bindSampler("opacity", pxr::TfToken("opacity"), pxr::TfToken("r"))
      && !bindAttribute("opacity", pxr::TfToken("opacity")))
    m_opacity = getParam<float>("opacity", 1.f);
  m_metallic = 0.f;
  m_roughness = 1.f;
  m_emissive = float3(0.f, 0.f, 0.f);
  m_alphaMode = AlphaMode::Opaque;
}

// PhysicallyBased ////////////////////////////////////////////////////////////

PhysicallyBased::PhysicallyBased(HydraGlobalState *s) : Material(s) {}

// glTF-style parameters with no UsdPreviewSurface target (SPEC §6.5): warned and
// dropped when the app provides them.
static const char *const kWarnAndDrop[] = {"transmission", "thickness",
    "attenuationColor", "attenuationDistance", "sheenColor", "sheenRoughness",
    "iridescence", "iridescenceIor", "iridescenceThickness", "specular",
    "specularColor", "clearcoatNormal"};

void PhysicallyBased::commitParameters()
{
  Material::commitParameters();
  // Each mapped slot accepts a constant, a sampler, or an attribute (SPEC §6.1);
  // sampler wins over attribute wins over the constant read.
  if (!bindSampler("baseColor", pxr::TfToken("diffuseColor"), pxr::TfToken("rgb"))
      && !bindAttribute("baseColor", pxr::TfToken("diffuseColor")))
    m_color = getParam<float3>("baseColor", float3(1.f, 1.f, 1.f));
  if (!bindSampler("metallic", pxr::TfToken("metallic"), pxr::TfToken("r"))
      && !bindAttribute("metallic", pxr::TfToken("metallic")))
    m_metallic = getParam<float>("metallic", 1.f);
  if (!bindSampler("roughness", pxr::TfToken("roughness"), pxr::TfToken("r"))
      && !bindAttribute("roughness", pxr::TfToken("roughness")))
    m_roughness = getParam<float>("roughness", 1.f);
  if (!bindSampler("emissive", pxr::TfToken("emissiveColor"), pxr::TfToken("rgb"))
      && !bindAttribute("emissive", pxr::TfToken("emissiveColor")))
    m_emissive = getParam<float3>("emissive", float3(0.f, 0.f, 0.f));
  if (!bindSampler("occlusion", pxr::TfToken("occlusion"), pxr::TfToken("r"))
      && !bindAttribute("occlusion", pxr::TfToken("occlusion")))
    m_occlusion = getParam<float>("occlusion", 1.f);
  if (!bindSampler("ior", pxr::TfToken("ior"), pxr::TfToken("r"))
      && !bindAttribute("ior", pxr::TfToken("ior")))
    m_ior = getParam<float>("ior", 1.5f);
  if (!bindSampler("clearcoat", pxr::TfToken("clearcoat"), pxr::TfToken("r"))
      && !bindAttribute("clearcoat", pxr::TfToken("clearcoat")))
    m_clearcoat = getParam<float>("clearcoat", 0.f);
  if (!bindSampler("clearcoatRoughness", pxr::TfToken("clearcoatRoughness"),
          pxr::TfToken("r"))
      && !bindAttribute(
          "clearcoatRoughness", pxr::TfToken("clearcoatRoughness")))
    m_clearcoatRoughness = getParam<float>("clearcoatRoughness", 0.f);

  // normal -> UsdPreviewSurface `normal` (SPEC §6.5): a tangent-space normal
  // map from any sampler, whose value is used as is: per ANARI the app's
  // outTransform/outOffset already decodes it to [-1,1]. A transform/primitive
  // sampler has no texture, and Storm's UsdPreviewSurface perturbs the normal
  // only from a texture, so for those the MaterialBuilder adds a Storm-only
  // network on a generated variant of the shader that reads the baked primvar
  // (SPEC §6.5). A non-sampler `normal` (a
  // constant/attribute) has no faithful target: warned and dropped, and the
  // surface shades from its geometric normal.
  if (!bindSampler("normal", pxr::TfToken("normal"), pxr::TfToken("rgb"),
          /* normalMap = */ true)
      && hasParam("normal")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "physicallyBased 'normal' accepts only a sampler (normal map) in v1; "
        "non-sampler value dropped (SPEC §6.5)");
  }

  // alphaMode/alphaCutoff -> opacity/opacityThreshold (SPEC §6.5): opaque forces
  // opacity 1, mask uses the cutoff threshold, blend passes opacity through.
  const std::string mode = getParamString("alphaMode", "opaque");
  const float cutoff = getParam<float>("alphaCutoff", 0.5f);
  if (mode == "mask") {
    m_alphaMode = AlphaMode::Mask;
    m_opacityThreshold = cutoff;
  } else if (mode == "blend") {
    m_alphaMode = AlphaMode::Blend;
    m_opacityThreshold = 0.f;
  } else {
    m_alphaMode = AlphaMode::Opaque;
    m_opacity = 1.f;
    m_opacityThreshold = 0.f;
  }

  // An opaque material must not author an opacity connection: a connection
  // overrides the constant 1 in the Hydra material network.
  if (m_alphaMode != AlphaMode::Opaque
      && !bindSampler("opacity", pxr::TfToken("opacity"), pxr::TfToken("r"))
      && !bindAttribute("opacity", pxr::TfToken("opacity")))
    m_opacity = getParam<float>("opacity", 1.f);

  for (const char *p : kWarnAndDrop) {
    if (hasParam(p)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "physicallyBased parameter '%s' has no UsdPreviewSurface target; "
          "dropped (SPEC §6.5)",
          p);
    }
  }
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Material *);
}
