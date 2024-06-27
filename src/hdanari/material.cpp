// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "material.h"
#include "debugCodes.h"
#include "material/usdPreviewSurface.h"

// pxr
#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hio/image.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
#include <pxr/usdImaging/usdImaging/tokens.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(HdAnariMaterialTokens,
  (bias)
  (clearcoat)
  (clearcoatRoughness)
  (diffuseColor)
  (emissiveColor)
  (fallback)
  (file)
  (filename)
  (infoFilename)
  (ior)
  (metallic)
  (normal)
  (occlusion)
  (opacity)
  (opacityThreshold)
  (roughness)
  (scale)
  (sourceColorSpace)
  (specularColor)
  (st)
  (surface)
  (UsdPreviewSurface)
  (UsdUVTexture)
  (useSpecularWorkflow)
  (wrapS)
  (wrapT)
);
// clang-format on

// Helper functions ///////////////////////////////////////////////////////////

// Rip-off from USD/HdSt code
HdMaterialNetwork2
HdConvertToHdMaterialNetwork2(
    const HdMaterialNetworkMap & hdNetworkMap,
    bool *isVolume)
{
    HD_TRACE_FUNCTION();
    HdMaterialNetwork2 result;

    for (auto const& iter: hdNetworkMap.map) {
        const TfToken & terminalName = iter.first;
        const HdMaterialNetwork & hdNetwork = iter.second;

        // Check if there are nodes associated with the volume terminal
        // This value is used in Storm to get the proper glslfx fragment shader
        if (terminalName == HdMaterialTerminalTokens->volume && isVolume) {
            *isVolume = !hdNetwork.nodes.empty();
        }

        // Transfer over individual nodes.
        // Note that the same nodes may be shared by multiple terminals.
        // We simply overwrite them here.
        if (hdNetwork.nodes.empty()) {
            continue;
        }
        for (const HdMaterialNode & node : hdNetwork.nodes) {
            HdMaterialNode2 & materialNode2 = result.nodes[node.path];
            materialNode2.nodeTypeId = node.identifier;
            materialNode2.parameters = node.parameters;
        }
        // Assume that the last entry is the terminal 
        result.terminals[terminalName].upstreamNode = 
                hdNetwork.nodes.back().path;

        // Transfer relationships to inputConnections on receiving/downstream nodes.
        for (const HdMaterialRelationship & rel : hdNetwork.relationships) {
            
            // outputId (in hdMaterial terms) is the input of the receiving node
            auto iter = result.nodes.find(rel.outputId);

            // skip connection if the destination node doesn't exist
            if (iter == result.nodes.end()) {
                continue;
            }

            std::vector<HdMaterialConnection2> &conns =
                iter->second.inputConnections[rel.outputName];
            HdMaterialConnection2 conn {rel.inputId, rel.inputName};
            
            // skip connection if it already exists (it may be shared
            // between surface and displacement)
            if (std::find(conns.begin(), conns.end(), conn) == conns.end()) {
                conns.push_back(std::move(conn));
            }
        }

        // Transfer primvars:
        result.primvars = hdNetwork.primvars;
    }
    return result;
}


// HdAnariMaterial ////////////////////////////////////////////////////////////

HdAnariMaterial::HdAnariMaterial(anari::Device d, const SdfPath &id)
  : HdMaterial(id) {}

HdAnariMaterial::~HdAnariMaterial() = default;

// HdAnariMatteMaterial ////////////////////////////////////////////////////////////

HdAnariMatteMaterial::HdAnariMatteMaterial(anari::Device d, const SdfPath &id)
    : HdAnariMaterial(d, id)
{
  TF_DEBUG_MSG(HD_ANARI_MATERIAL, "Creating new Matte material %s\n", id.GetText());

  if (!d)
    return;

  anari::retain(d, d);

  _anari.device = d;
  _anari.material = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, _anari.material, "alphaMode", "blend");
  anari::setParameter(d, _anari.material, "color", GfVec3f{1.0f, 0.0f, 1.0f});
  anari::commitParameters(d, _anari.material);
}

HdAnariMatteMaterial::~HdAnariMatteMaterial()
{
  if (!_anari.device)
    return;

  anari::release(_anari.device, _anari.material);
}

void HdAnariMatteMaterial::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  if ((*dirtyBits & HdMaterial::DirtyResource) == 0) {
    return;
  }

  //  find material network. Focusing only HdMaterialNetwork, keeping HdMaterialNetwork2 for later
  VtValue networkMapResource = sceneDelegate->GetMaterialResource(GetId());
  HdMaterialNetworkMap networkMap = networkMapResource.Get<HdMaterialNetworkMap>();

  // Get surface terminal for this material network
  auto materialNetworkMapIt = networkMap.map.find(HdAnariMaterialTokens->surface);
  if (materialNetworkMapIt == networkMap.map.end()) {
    return; // no surface terminial
  }

  HdMaterialNetwork surfaceMaterialNetwork = materialNetworkMapIt->second;

  bool isVolume = false;
  HdMaterialNetwork2 materialNetwork2 = HdConvertToHdMaterialNetwork2(networkMap, &isVolume);
  if (isVolume) {
    TF_WARN("Volume material are not yet supported");
    return;
  }

  HdMaterialNetwork2Interface materialNetworkIface = HdMaterialNetwork2Interface(GetId(), &materialNetwork2);

  auto con = materialNetworkIface.GetTerminalConnection(HdMaterialTerminalTokens->surface);
  if (!con.first) {
    TF_WARN("Cannot find a surface terminal on prim %s", GetId().GetText());
    return;
  }
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    _ProcessUsdPreviewSurfaceNode(materialNetworkIface, terminalNode);
  }

  *dirtyBits &= ~DirtyBits::DirtyResource;
}

HdDirtyBits HdAnariMatteMaterial::GetInitialDirtyBitsMask() const
{
  return AllDirty;
}

anari::Material HdAnariMatteMaterial::GetANARIMaterial() const
{
  return _anari.material;
}

void HdAnariMatteMaterial::_ProcessUsdPreviewSurfaceNode(const HdMaterialNetwork2Interface& materialNetwork, TfToken terminal)
{
  HdAnariUsdPreviewSurfaceConverter upsc(_anari.device, _anari.material);
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->diffuseColor, "baseColor", GfVec3f(0.18f, 0.18f, 0.18f));
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->opacity, "opacity", 1.0f);

  switch (upsc.GetAlphaMode(materialNetwork, terminal)) {
    case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Blend:{
      anari::setParameter(_anari.device, _anari.material, "alphaMode", "blend");
            anari::unsetParameter(_anari.device, _anari.material, "alphaCutoff");

       break;
    }
    case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Opaque: {
      anari::setParameter(_anari.device, _anari.material, "alphaMode", "opaque");
            anari::unsetParameter(_anari.device, _anari.material, "alphaCutoff");

      break;
    }
    case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Mask: {
      anari::setParameter(_anari.device, _anari.material, "alphaMode", "mask");
      upsc.ProcessInputValue(materialNetwork, terminal, HdAnariMaterialTokens->opacityThreshold, "alphaCutoff", 0.5f);
      break;
    }
  }

  anari::commitParameters(_anari.device, _anari.material);
}

// HdAnariPhysicallyBasedMaterial ////////////////////////////////////////////////////////////

HdAnariPhysicallyBasedMaterial::HdAnariPhysicallyBasedMaterial(anari::Device d, const SdfPath &id)
  : HdAnariMaterial(d, id)
{
  TF_DEBUG_MSG(HD_ANARI_MATERIAL, "Creating new PhysicallyBased material %s\n", id.GetText());
  if (!d)
    return;

  anari::retain(d, d);

  _anari.device = d;
  _anari.material = anari::newObject<anari::Material>(d, "physicallyBased");
  anari::setParameter(d, _anari.material, "alphaMode", "blend");
  anari::setParameter(d, _anari.material, "baseColor", GfVec3f{1.0f, 0.0f, 1.0f});
  anari::commitParameters(d, _anari.material);
}

HdAnariPhysicallyBasedMaterial::~HdAnariPhysicallyBasedMaterial()
{
  if (!_anari.device)
    return;

  anari::release(_anari.device, _anari.material);
}

void HdAnariPhysicallyBasedMaterial::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  if ((*dirtyBits & HdMaterial::DirtyResource) == 0) {
    return;
  }

  //  find material network. Focusing only HdMaterialNetwork, keeping HdMaterialNetwork2 for later
  VtValue networkMapResource = sceneDelegate->GetMaterialResource(GetId());
  HdMaterialNetworkMap networkMap = networkMapResource.Get<HdMaterialNetworkMap>();

  // Get surface terminal for this material network
  auto materialNetworkMapIt = networkMap.map.find(HdAnariMaterialTokens->surface);
  if (materialNetworkMapIt == networkMap.map.end()) {
    return; // no surface terminial
  }

  HdMaterialNetwork surfaceMaterialNetwork = materialNetworkMapIt->second;

  bool isVolume = false;
  HdMaterialNetwork2 materialNetwork2 = HdConvertToHdMaterialNetwork2(networkMap, &isVolume);
  if (isVolume) {
    TF_WARN("Volume material are not yet supported");
    return;
  }

  HdMaterialNetwork2Interface materialNetworkIface = HdMaterialNetwork2Interface(GetId(), &materialNetwork2);

  auto con = materialNetworkIface.GetTerminalConnection(HdMaterialTerminalTokens->surface);
  if (!con.first) {
    TF_WARN("Cannot find a surface terminal on prim %s", GetId().GetText());
    return;
  }
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);
  if (terminalNodeType == HdAnariMaterialTokens->UsdPreviewSurface) {
    _ProcessUsdPreviewSurfaceNode(materialNetworkIface, terminalNode);
  }

  *dirtyBits &= ~DirtyBits::DirtyResource;
}

HdDirtyBits HdAnariPhysicallyBasedMaterial::GetInitialDirtyBitsMask() const
{
  return AllDirty;
}

anari::Material HdAnariPhysicallyBasedMaterial::GetANARIMaterial() const
{
  return _anari.material;
}

void HdAnariPhysicallyBasedMaterial::_ProcessUsdPreviewSurfaceNode(const HdMaterialNetwork2Interface& materialNetwork, TfToken terminal)
{
  HdAnariUsdPreviewSurfaceConverter upsc(_anari.device, _anari.material);
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->diffuseColor, "baseColor", GfVec3f(0.18f, 0.18f, 0.18f));
  upsc.ProcessInputConnection(materialNetwork, terminal, HdAnariMaterialTokens->diffuseColor, "normal"); // No default value to be set if no connection
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->opacity, "opacity", 1.0f);
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->metallic, "metallic", 0.0f);
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->roughness, "roughness", 0.5f);
  // FIXME: Emissive in only an intensity in USDPreviewSurface while it is a color for ANARI. Need to have a proper way to upcast float to float3.
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->emissiveColor, "emissive", GfVec3f(0.0f, 0.0f, 0.0));
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->clearcoat, "clearcoat", 0.0f);
  upsc.ProcessInput(materialNetwork, terminal, HdAnariMaterialTokens->clearcoatRoughness, "clearcoatRoughness", 0.01f);

  switch (upsc.GetAlphaMode(materialNetwork, terminal)) {
    case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Blend:{
      anari::setParameter(_anari.device, _anari.material, "alphaMode", "blend");
      anari::unsetParameter(_anari.device, _anari.material, "alphaCutoff");
      break;
    }
    case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Opaque: {
      anari::setParameter(_anari.device, _anari.material, "alphaMode", "opaque");
      anari::unsetParameter(_anari.device, _anari.material, "alphaCutoff");
      break;
    }
    case HdAnariUsdPreviewSurfaceConverter::AlphaMode::Mask: {
      anari::setParameter(_anari.device, _anari.material, "alphaMode", "mask");
      upsc.ProcessInputValue(materialNetwork, terminal, HdAnariMaterialTokens->opacityThreshold, "alphaCutoff", 0.5f);
      break;
    }
  }

  anari::commitParameters(_anari.device, _anari.material);
}


PXR_NAMESPACE_CLOSE_SCOPE