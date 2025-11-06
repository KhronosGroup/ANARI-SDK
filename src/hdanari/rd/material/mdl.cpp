// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "mdl.h"

#include "../anariTypes.h"
#include "mdl/mdlRegistry.h"
#include "rd/material.h"
#include "rd/material/usdPreviewSurfaceConverter.h"
#include "rd/materialTokens.h"

#include <mi/neuraylib/imdl_impexp_api.h>
#include <mi/neuraylib/istring.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/usdShade/nodeDefAPI.h>

#include <anari/anari_cpp.hpp>

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;

PXR_NAMESPACE_OPEN_SCOPE

anari::Material HdAnariMdlMaterial::CreateMaterial(anari::Device device,
    const HdMaterialNetwork2Interface &materialNetworkIface)
{
  return anari::newObject<anari::Material>(device, "mdl");
}

void HdAnariMdlMaterial::SyncMaterialParameters(anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    const HdAnariMaterial::PrimvarBinding &primvarBinding,
    const HdAnariMaterial::PrimvarMapping &primvarMapping,
    const HdAnariMaterial::SamplerMapping &samplerMapping)
{
  auto con = materialNetworkIface.GetTerminalConnection(
      HdMaterialTerminalTokens->surface);
  if (con.first) {
    auto terminalNode = con.second.upstreamNodeName;
    auto terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);

  ProcessMdlNode(device,
        material,
        materialNetworkIface,
        terminalNode,
        primvarBinding,
        primvarMapping,
        samplerMapping);
  } else {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
  }
}

void HdAnariMdlMaterial::ProcessMdlNode(anari::Device device,
    anari::Material material,
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken terminal,
    const HdAnariMaterial::PrimvarBinding &primvarBinding,
    const HdAnariMaterial::PrimvarMapping &primvarMapping,
    const HdAnariMaterial::SamplerMapping &samplerMapping)
{
  auto id = materialNetworkIface.GetMaterialPrimPath();

  auto terminalNodeType = materialNetworkIface.GetNodeType(terminal);
  auto shaderNode = SdrRegistry::GetInstance().GetShaderNodeByIdentifierAndType(
      terminalNodeType, HdAnariMaterialTokens->mdl);

  auto mdlRegistryInstance = HdAnariMdlRegistry::GetInstance();

  auto uri = shaderNode->GetResolvedImplementationURI();

  if (uri.substr(0, 2) != "::"s) {
    // Try and get a module name from the URI
    auto impexp = make_handle(mdlRegistryInstance->getINeuray()
            ->get_api_component<mi::neuraylib::IMdl_impexp_api>());
    auto moduleName = std::string();
    if (auto moduleNameS = make_handle(
            impexp->get_mdl_module_name(("file:///"s + uri).c_str()));
        moduleNameS.is_valid_interface()) {
      moduleName = moduleNameS->get_c_str();
    }
    auto materialName = shaderNode->GetIdentifier().GetString();
    materialName = materialName.substr(materialName.find_first_of('<'));
    materialName = materialName.substr(materialName.find_first_not_of('<'));
    materialName = materialName.substr(0, materialName.find_first_of('>'));

    if (!moduleName.empty() && !materialName.empty()) {
      uri = moduleName + "::"s + materialName;
    }
  }

  anari::setParameter(device, material, "source", uri);
  anari::setParameter(device, material, "sourceType", "module");

  for (auto name :
      materialNetworkIface.GetAuthoredNodeParameterNames(terminal)) {
    auto nodeName = terminal;
    auto inputName = name;
    if (inputName.GetString().substr(0, 9) == "typeName:") {
      continue; // Skip typeName parameters
    }

    auto cnxs = materialNetworkIface.GetNodeInputConnection(terminal, name);
    if (cnxs.size()) {
      auto cnx = cnxs.front();
      nodeName = cnx.upstreamNodeName;
      inputName = cnx.upstreamOutputName;
    }

    auto value =
        materialNetworkIface.GetNodeParameterValue(nodeName, inputName);
    if (value.IsHolding<bool>()) {
      anari::setParameter(
          device, material, name.GetText(), value.UncheckedGet<bool>());
    } else if (value.IsHolding<int>()) {
      anari::setParameter(
          device, material, name.GetText(), value.UncheckedGet<int>());
    } else if (value.IsHolding<GfVec2i>()) {
      auto v = value.UncheckedGet<GfVec2i>();
      anari::setParameter<int[2]>(
          device, material, name.GetText(), {v[0], v[1]});
    } else if (value.IsHolding<GfVec3i>()) {
      auto v = value.UncheckedGet<GfVec3i>();
      anari::setParameter<int[3]>(
          device, material, name.GetText(), {v[0], v[1], v[2]});
    } else if (value.IsHolding<GfVec4i>()) {
      auto v = value.UncheckedGet<GfVec4i>();
      anari::setParameter<int[4]>(
          device, material, name.GetText(), {v[0], v[1], v[2], v[3]});
    } else if (value.IsHolding<float>()) {
      anari::setParameter(
          device, material, name.GetText(), value.UncheckedGet<float>());
    } else if (value.IsHolding<GfVec2f>()) {
      auto v = value.UncheckedGet<GfVec2f>();
      anari::setParameter<float[2]>(
          device, material, name.GetText(), {v[0], v[1]});
    } else if (value.IsHolding<GfVec3f>()) {
      auto v = value.UncheckedGet<GfVec3f>();
      anari::setParameter<float[3]>(
          device, material, name.GetText(), {v[0], v[1], v[2]});
    } else if (value.IsHolding<GfVec4f>()) {
      auto v = value.UncheckedGet<GfVec4f>();
      anari::setParameter<float[4]>(
          device, material, name.GetText(), {v[0], v[1], v[2], v[3]});
    } else if (value.IsHolding<TfToken>()) {
      auto n = name.GetString();
      auto v = value.UncheckedGet<TfToken>().GetString();

      static constexpr const auto colorSpacePrefix = "colorSpace:"sv;
      if (n.substr(0, size(colorSpacePrefix)) == colorSpacePrefix) {
        n = n.substr(size(colorSpacePrefix)) + ".colorspace"s;
        for (auto &c : v)
          c = std::tolower(c);
      }
      anari::setParameter(device, material, n.c_str(), v);
    } else if (value.IsHolding<SdfAssetPath>()) {
      auto path = value.UncheckedGet<SdfAssetPath>().GetResolvedPath();
      if (path.empty()) {
        path = value.UncheckedGet<SdfAssetPath>().GetAssetPath();
      }
      if (!path.empty()) {
        anari::setParameter(device, material, name.GetText(), path);
      }
    } else {
      TF_WARN("Don't know how to handle %s of type %s",
          name.GetText(), value.GetTypeName().c_str());
    }
  }
}

HdAnariMaterial::PrimvarMapping HdAnariMdlMaterial::EnumeratePrimvars(
    const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal)
{
  auto con = materialNetworkIface.GetTerminalConnection(terminal);
  if (!con.first) {
    TF_CODING_ERROR("Cannot find a surface terminal on prim %s",
        materialNetworkIface.GetMaterialPrimPath().GetText());
    return {};
  }

  // Fake it so we are mapping primvars:st on the mesh to attribute0.
  // FIXME: Find out how to map more than one texture coordinate. Maybe by
  // checking for some primvar role if any?
  TfToken terminalNode = con.second.upstreamNodeName;
  TfToken terminalNodeType = materialNetworkIface.GetNodeType(terminalNode);

  return {{materialNetworkIface.GetMaterialPrimPath(), TfToken("st")}};
}

HdAnariMaterial::TextureDescMapping HdAnariMdlMaterial::EnumerateTextures(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken terminalName)
{
  // Nothing specific to enumerate for textures
  return {};
}

PXR_NAMESPACE_CLOSE_SCOPE
