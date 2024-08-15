// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "matte.h"

#include "usdPreviewSurfaceConverter.h"
#include "../materialTokens.h"

PXR_NAMESPACE_OPEN_SCOPE

HdAnariMaterial::TextureDescMapping HdAnariMatteMaterial::EnumerateTextures(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken terminal) const
{
  auto con = materialNetworkIface.GetTerminalConnection(terminal);
  if (!con.first) {
    TF_WARN("Cannot find a surface terminal on prim %s", GetId().GetText());
    return {};
  }
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    auto usdPreviewSurfaceName = terminalNode;
    // FIXME: WE should only keep useful textures there (Matte is only about diffuse and opacity).
    return HdAnariUsdPreviewSurfaceConverter::EnumerateTextures(materialNetworkIface, usdPreviewSurfaceName);
  }

  return {};
}

HdAnariMaterial::PrimvarMapping HdAnariMatteMaterial::EnumeratePrimvars(const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal) const {
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

  return { { materialNetworkIface.GetMaterialPrimPath(), TfToken("color")} };
}

anari::Material HdAnariMatteMaterial::GetOrCreateMaterial(
    const HdMaterialNetwork2Interface& materialNetworkIface,
    const PrimvarBinding& primvarBinding,
    const PrimvarMapping& primvarMapping,
    const SamplerMapping& samplerMapping
  ) const
{
  auto material = anari::newObject<anari::Material>(device_, "matte");

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

void HdAnariMatteMaterial::ProcessUsdPreviewSurfaceNode(anari::Material material, const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal,
    const PrimvarBinding& primvarBinding, const PrimvarMapping& primvarMapping, const SamplerMapping& samplerMapping) const
{
  HdAnariUsdPreviewSurfaceConverter upsc(device_, material, 
      &materialNetworkIface, terminal,
      &primvarBinding, &primvarMapping, &samplerMapping);

  upsc.ProcessInput(HdAnariMaterialTokens->diffuseColor,
      "color",
      GfVec3f(0.18f, 0.18f, 0.18f),
      HdAnariTextureLoader::ColorSpace::SRgb);
  upsc.ProcessInput(HdAnariMaterialTokens->opacity,
      "opacity",
      1.0f,
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
