// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "physicallyBased.h"

#include "usdPreviewSurfaceConverter.h"
#include "../materialTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

HdAnariMaterial::TextureDescMapping HdAnariPhysicallyBasedMaterial::EnumerateTextures(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken terminalName) const
{
  auto con = materialNetworkIface.GetTerminalConnection(terminalName);
  if (!con.first) {
    TF_WARN("Cannot find a surface terminal on prim %s", GetId().GetText());
    return {};
  }
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    return HdAnariUsdPreviewSurfaceConverter::EnumerateTextures(materialNetworkIface, terminalNode);
  }

  return {};
}

std::map<SdfPath, TfToken> HdAnariPhysicallyBasedMaterial::EnumeratePrimvars(const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal) const {
  auto con = materialNetworkIface.GetTerminalConnection(terminal);
  if (!con.first) {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s", GetId().GetText());
    return {};
  }

  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);

  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    return HdAnariUsdPreviewSurfaceConverter::EnumeratePrimvars(materialNetworkIface, terminalNode);
  }

  return {};
}

anari::Material HdAnariPhysicallyBasedMaterial::GetOrCreateMaterial(
    const HdMaterialNetwork2Interface& materialNetworkIface,
    const PrimvarBinding& primvarBinding,
    const PrimvarMapping& primvarMapping,
    const SamplerMapping& samplerMapping
  ) const
{
  auto material = anari::newObject<anari::Material>(device_, "physicallyBased");

  auto con = materialNetworkIface.GetTerminalConnection(HdMaterialTerminalTokens->surface);
  if (con.first) {
    TfToken terminalNode = con.second.upstreamNodeName;
    TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
    if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {  
      ProcessUsdPreviewSurfaceNode(material, materialNetworkIface, terminalNode, primvarBinding, primvarMapping, samplerMapping);
    }
  } else {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s", GetId().GetText());
  }

  return material;
}


void HdAnariPhysicallyBasedMaterial::ProcessUsdPreviewSurfaceNode(anari::Material material, const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal,
    const PrimvarBinding& primvarBinding, const PrimvarMapping& primvarMapping, const SamplerMapping& samplerMapping) const
{
  HdAnariUsdPreviewSurfaceConverter upsc(device_, material, 
      &materialNetworkIface, terminal,
      &primvarBinding, &primvarMapping, &samplerMapping);

  upsc.ProcessInput(HdAnariMaterialTokens->diffuseColor,
      "baseColor",
      GfVec3f(1.18f, 0.18f, 0.18f),
      HdAnariTextureLoader::ColorSpace::SRgb);
  upsc.ProcessInputConnection(HdAnariMaterialTokens->normal,
      "normal",
      HdAnariTextureLoader::ColorSpace::
          Raw); // No default value to be set if no connection
  upsc.ProcessInput(HdAnariMaterialTokens->opacity,
      "opacity",
      1.0f,
      HdAnariTextureLoader::ColorSpace::Raw);
  upsc.ProcessInput(HdAnariMaterialTokens->metallic,
      "metallic",
      0.0f,
      HdAnariTextureLoader::ColorSpace::Raw);
  upsc.ProcessInput(HdAnariMaterialTokens->roughness,
      "roughness",
      0.5f,
      HdAnariTextureLoader::ColorSpace::Raw);
  // FIXME: Emissive in only an intensity in USDPreviewSurface while it is a
  // color for ANARI. Need to have a proper way to upcast float to float3.
  upsc.ProcessInput(HdAnariMaterialTokens->emissiveColor,
      "emissive",
      GfVec3f(0.0f, 0.0f, 0.0),
      HdAnariTextureLoader::ColorSpace::SRgb);
  upsc.ProcessInput(HdAnariMaterialTokens->clearcoat,
      "clearcoat",
      0.0f,
      HdAnariTextureLoader::ColorSpace::Raw);
  upsc.ProcessInput(HdAnariMaterialTokens->clearcoatRoughness,
      "clearcoatRoughness",
      0.01f,
      HdAnariTextureLoader::ColorSpace::Raw);

  switch (upsc.GetAlphaMode(materialNetworkIface, terminal)) {
  case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Blend: {
    anari::setParameter(device_, material, "alphaMode", "blend");
    anari::unsetParameter(device_, material, "alphaCutoff");
    break;
  }
  case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Opaque: {
    anari::setParameter(device_, material, "alphaMode", "opaque");
    anari::unsetParameter(device_, material, "alphaCutoff");
    break;
  }
  case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Mask: {
    anari::setParameter(device_, material, "alphaMode", "mask");
    upsc.ProcessInputValue(HdAnariMaterialTokens->opacityThreshold,
        "alphaCutoff",
        0.5f);
    break;
  }
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
