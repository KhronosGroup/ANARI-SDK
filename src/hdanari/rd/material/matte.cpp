// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "matte.h"

#include "../anariTypes.h"
#include "../materialTokens.h"
#include "usdPreviewSurfaceConverter.h"

#include <anari/anari_cpp.hpp>

PXR_NAMESPACE_OPEN_SCOPE

HdAnariMaterial::TextureDescMapping HdAnariMatteMaterial::EnumerateTextures(
    const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal)
{
  auto con = materialNetworkIface.GetTerminalConnection(terminal);
  if (!con.first) {
    TF_WARN("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
    return {};
  }
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    auto usdPreviewSurfaceName = terminalNode;
    // FIXME: WE should only keep useful textures there (Matte is only about
    // diffuse and opacity).
    return HdAnariUsdPreviewSurfaceConverter::EnumerateTextures(
        materialNetworkIface, usdPreviewSurfaceName);
  }

  return {};
}

HdAnariMaterial::PrimvarMapping HdAnariMatteMaterial::EnumeratePrimvars(
    const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal)
{
  auto con = materialNetworkIface.GetTerminalConnection(terminal);
  if (!con.first) {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
    return {};
  }

  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);

  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    return HdAnariUsdPreviewSurfaceConverter::EnumeratePrimvars(
        materialNetworkIface, terminalNode);
  }

  return {{materialNetworkIface.GetMaterialPrimPath(), TfToken("color")}};
}

anari::Material HdAnariMatteMaterial::CreateMaterial(anari::Device device,
    const HdMaterialNetwork2Interface &materialNetworkIface)
{
  return anari::newObject<anari::Material>(device, "matte");
}

void HdAnariMatteMaterial::SyncMaterialParameters(anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    const HdAnariMaterial::PrimvarBinding &primvarBinding,
    const HdAnariMaterial::PrimvarMapping &primvarMapping,
    const HdAnariMaterial::SamplerMapping &samplerMapping)
{
  auto con = materialNetworkIface.GetTerminalConnection(
      HdMaterialTerminalTokens->surface);
  if (con.first) {
    TfToken terminalNode = con.second.upstreamNodeName;
    TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
    if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
      ProcessUsdPreviewSurfaceNode(device,
          material,
          materialNetworkIface,
          terminalNode,
          primvarBinding,
          primvarMapping,
          samplerMapping);
    }
  } else {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
  }
}

void HdAnariMatteMaterial::ProcessUsdPreviewSurfaceNode(anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken terminal,
    const HdAnariMaterial::PrimvarBinding &primvarBinding,
    const HdAnariMaterial::PrimvarMapping &primvarMapping,
    const HdAnariMaterial::SamplerMapping &samplerMapping)
{
  HdAnariUsdPreviewSurfaceConverter upsc(device,
      material,
      &materialNetworkIface,
      terminal,
      &primvarBinding,
      &primvarMapping,
      &samplerMapping);

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
    anari::setParameter(device, material, "alphaMode", "blend");
    anari::unsetParameter(device, material, "alphaCutoff");
    break;
  }
  case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Opaque: {
    anari::setParameter(device, material, "alphaMode", "opaque");
    anari::unsetParameter(device, material, "alphaCutoff");
    break;
  }
  case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Mask: {
    anari::setParameter(device, material, "alphaMode", "mask");
    upsc.ProcessInputValue(
        HdAnariMaterialTokens->opacityThreshold, "alphaCutoff", 0.5f);
    break;
  }
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
