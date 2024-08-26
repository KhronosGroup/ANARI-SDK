// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "usdPreviewSurfaceConverter.h"
#include "textureLoader.h"
#include "../materialTokens.h"

#include "../debugCodes.h"
#include "../hdAnariTypes.h"

#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usdImaging/usdImaging/tokens.h>

#include <anari/anari_cpp.hpp>
#include <string>

using namespace std::string_literals;
PXR_NAMESPACE_OPEN_SCOPE

const std::set<TfToken> HdAnariUsdPreviewSurfaceConverter::inputIsSrgb = {
    HdAnariMaterialTokens->diffuseColor
};

HdAnariMaterial::TextureDescMapping
HdAnariUsdPreviewSurfaceConverter::EnumerateTextures(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken usdPreviewSurfaceNodeName)
{
  HdAnariMaterial::TextureDescMapping textures;

  for (const auto &inputName : materialNetworkIface.GetNodeInputConnectionNames(
           usdPreviewSurfaceNodeName)) {
    auto inputConnections = materialNetworkIface.GetNodeInputConnection(
        usdPreviewSurfaceNodeName, inputName);
    if (inputConnections.empty())
      continue;

    if (inputConnections.size() > 1) {
      TF_DEBUG_MSG(HD_ANARI_MATERIAL,
          "Connected to more than 1 input. Skipping %s:%s\n",
          usdPreviewSurfaceNodeName.GetText(),
          inputName.GetText());
      continue;
    }

    auto inputNodeName = inputConnections.front().upstreamNodeName;
    auto inputNodeType = materialNetworkIface.GetNodeType(inputNodeName);

    if (inputNodeType != UsdImagingTokens->UsdUVTexture)
      continue;

    auto inputTextureName = inputNodeName;
    auto outputName = inputConnections.front().upstreamOutputName;

    // File name
    auto file = materialNetworkIface.GetNodeParameterValue(
        inputTextureName, HdAnariMaterialTokens->file);
    auto colorspaceVt = materialNetworkIface.GetNodeParameterValue(
        inputTextureName, HdAnariMaterialTokens->sourceColorSpace);
    TfToken colorspaceTk;

    if (!colorspaceVt.IsEmpty()) {
      if (colorspaceVt.IsHolding<TfToken>()) {
        colorspaceTk = colorspaceVt.UncheckedGet<TfToken>();
        if (colorspaceTk != HdAnariMaterialTokens->raw
            && colorspaceTk != HdAnariMaterialTokens->sRGB) {
          TF_WARN(
              "Invalid colorspace value %s, ignoring\n", colorspaceTk.GetText());
          colorspaceTk = TfToken();
        }
      } else
      TF_WARN("Invalid colorspace value type %s, ignoring\n",
          colorspaceVt.GetTypeName().c_str());
    }

    HdAnariTextureLoader::ColorSpace colorspace;
    if (colorspaceTk == HdAnariMaterialTokens->raw) {
      colorspace = HdAnariTextureLoader::ColorSpace::Raw;
    } else if (colorspaceTk == HdAnariMaterialTokens->sRGB) {
      colorspace = HdAnariTextureLoader::ColorSpace::SRgb;
    } else {
       colorspace = inputIsSrgb.find(inputName) != cend(inputIsSrgb) ? HdAnariTextureLoader::ColorSpace::SRgb : HdAnariTextureLoader::ColorSpace::Raw;
    }


    auto assetPath = file.Get<SdfAssetPath>().GetResolvedPath();

    auto tx = std::array{
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
      };
    // If connnected to a specific channel, swizzle the color components to
    // mimic the connection behavior
    if (outputName == HdAnariMaterialTokens->r) {
      // clang-format off
      tx = {
          1.0f, 1.0f, 1.0f, 1.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
      };
      // clang-format on

    } else if (outputName == HdAnariMaterialTokens->g) {
      // clang-format off
      tx = {
          0.0f, 0.0f, 0.0f, 0.0f,
          1.0f, 1.0f, 1.0f, 1.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
      };
      // clang-format on

    } else if (outputName == HdAnariMaterialTokens->b) {
      // clang-format off
      tx = {
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          1.0f, 1.0f, 1.0f, 1.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
      };
      // clang-format on

    } else if (outputName == HdAnariMaterialTokens->a) {
      // clang-format off
      tx = {
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 0.0f,
          1.0f, 1.0f, 1.0f, 1.0f,
      };
      // clang-format on

    }

    textures[SdfPath(inputTextureName).AppendProperty(outputName)] = HdAnariTextureLoader::TextureDesc{
      assetPath,
      colorspace,
       HdAnariTextureLoader::MinMagFilter::Linear, // FIXME: Should be coming from the UsdShade node instead
       tx
    };
  }

  return textures;
}

HdAnariMaterial::PrimvarMapping HdAnariUsdPreviewSurfaceConverter::EnumeratePrimvars(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken usdPreviewSurfaceNodeName)
{
  HdAnariMaterial::PrimvarMapping primvars;

  for (const auto &inputName : materialNetworkIface.GetNodeInputConnectionNames(
           usdPreviewSurfaceNodeName)) {
    auto inputConnections = materialNetworkIface.GetNodeInputConnection(
        usdPreviewSurfaceNodeName, inputName);
    if (inputConnections.empty()) continue;

    if (inputConnections.size() > 1) {
      TF_DEBUG_MSG(HD_ANARI_MATERIAL,
          "Connected to more than 1 input. Skipping %s:%s\n",
          usdPreviewSurfaceNodeName.GetText(),
          inputName.GetText());
      continue;
    }

    auto upstreamNodeName = inputConnections.front().upstreamNodeName;
    auto upstreamNodeType = materialNetworkIface.GetNodeType(upstreamNodeName);


    if (upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float
        || upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float2
        || upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float3
        || upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float3
        || upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float4
        || upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_int) {
      auto inputPrimvarReaderName = upstreamNodeName;
      auto outputName = inputConnections.front().upstreamOutputName;

      auto primvarName = materialNetworkIface.GetNodeParameterValue(inputPrimvarReaderName, HdAnariMaterialTokens->varname);
      if (primvarName.CanCast<TfToken>()) {
        auto primvar = primvarName.Cast<TfToken>().UncheckedGet<TfToken>();
        primvars.insert(std::make_pair(SdfPath(upstreamNodeName), primvar));
      } else {
        TF_WARN("%s specifies an invalid type %s for input %s. Skpping.\n", inputPrimvarReaderName.GetText(), primvarName.GetTypeName().c_str(), outputName.GetText());
      }
    } else {
      auto otherpvs = EnumeratePrimvars(materialNetworkIface, upstreamNodeName);
      primvars.insert(cbegin(otherpvs), cend(otherpvs));
    }
  }

  return primvars;
}

HdAnariUsdPreviewSurfaceConverter::HdAnariUsdPreviewSurfaceConverter(
  anari::Device device, anari::Material material,
  const HdMaterialNetwork2Interface* materialNetworkIface, const TfToken& terminalNode,
  const HdAnariMaterial::PrimvarBinding* primvarBinding, const HdAnariMaterial::PrimvarMapping* primvarMapping, const HdAnariMaterial::SamplerMapping* samplerMapping)
        : device_(device), material_(material),
          materialNetworkIface_(materialNetworkIface), terminalNode_(terminalNode),
          primvarBinding_(primvarBinding), primvarMapping_(primvarMapping),
        samplerMapping_(samplerMapping) {
    }


bool HdAnariUsdPreviewSurfaceConverter::ProcessConnection(
    TfToken usdInputName,
    const char *anariParameterName,
    HdAnariTextureLoader::ColorSpace fallbackColorspace)
{
  auto inputConnections = materialNetworkIface_->GetNodeInputConnection(terminalNode_, usdInputName);
  if (inputConnections.empty())
    return false;

  if (inputConnections.size() > 1) {
    TF_DEBUG_MSG(HD_ANARI_MATERIAL,
        "Connected to more than 1 input. Skipping %s:%s\n",
        terminalNode_.GetText(),
        usdInputName.GetText());
  }

  auto upstreamNodeName = inputConnections.front().upstreamNodeName;
  auto upstreamNodeType = materialNetworkIface_->GetNodeType(upstreamNodeName);

  if (upstreamNodeType == UsdImagingTokens->UsdUVTexture) {
    auto upstreamOutputName = inputConnections.front().upstreamOutputName;
    ProcessUSDUVTexture(upstreamNodeName, upstreamOutputName,  anariParameterName);
  } else if (upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float) {
    ProcessPrimvarReader<float>(upstreamNodeName, material_, anariParameterName);
  } else if (upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float2) {
    ProcessPrimvarReader<GfVec2f>(upstreamNodeName, material_, anariParameterName);
  } else if (upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float3) {
    ProcessPrimvarReader<GfVec3f>(upstreamNodeName, material_, anariParameterName);
  } else if (upstreamNodeType == UsdImagingTokens->UsdPrimvarReader_float4) {
    ProcessPrimvarReader<GfVec4f>(upstreamNodeName, material_, anariParameterName);
  } else {
    TF_WARN("Unsupported input type %s for %s:%s\n",
        upstreamNodeType.GetText(),
        terminalNode_.GetText(),
        usdInputName.GetText());
    return false;
  }

  return true;
}

bool HdAnariUsdPreviewSurfaceConverter::ProcessValue(
    TfToken usdInputName,
    const char *anariParameterName,
    HdAnariTextureLoader::ColorSpace fallbackColorspace)
{
  auto value = materialNetworkIface_->GetNodeParameterValue(terminalNode_, usdInputName);
  if (value.GetType().IsA<float>()) {
    anari::setParameter(
        device_, material_, anariParameterName, value.Get<float>());
  } else if (value.GetType().IsA<GfVec2f>()) {
    anari::setParameter(
        device_, material_, anariParameterName, value.Get<GfVec2f>());
  } else if (value.GetType().IsA<GfVec3f>()) {
    anari::setParameter(
        device_, material_, anariParameterName, value.Get<GfVec3f>());
  } else if (value.GetType().IsA<GfVec4f>()) {
    anari::setParameter(
        device_, material_, anariParameterName, value.Get<GfVec4f>());
  } else if (value.GetType().IsA<void>()) {
    return false; // Special case for unassigned values. Consider them
                  // unauthored, instead of erroring out about void not being a
                  // valid type.
  } else {
    TF_WARN("Unsupported value type %s at %s:%s\n",
        value.GetTypeName().c_str(),
        terminalNode_.GetText(),
        usdInputName.GetText());
    return false;
  }

  return true;
}

HdAnariUsdPreviewSurfaceConverter::AlphaMode
HdAnariUsdPreviewSurfaceConverter::GetAlphaMode(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken usdPreviewSurfaceName)
{
  AlphaMode alphaMode = AlphaMode::Blend;

  auto vtOpacityThreshold = materialNetworkIface.GetNodeParameterValue(
      usdPreviewSurfaceName, HdAnariMaterialTokens->opacityThreshold);
  if (vtOpacityThreshold.IsHolding<float>()) {
    float opacityThreshold = vtOpacityThreshold.UncheckedGet<float>();
    if (opacityThreshold > 0.0f) {
      alphaMode = AlphaMode::Mask;
    }
  } else {
    auto vtOpacity = materialNetworkIface.GetNodeParameterValue(
        usdPreviewSurfaceName, HdAnariMaterialTokens->opacityThreshold);
    if (vtOpacity.IsHolding<float>()) {
      float opacity = vtOpacity.UncheckedGet<float>();
      if (opacity == 1.0f) {
        alphaMode = AlphaMode::Opaque;
      }
    }
  }

  return alphaMode;
}

bool HdAnariUsdPreviewSurfaceConverter::UseSpecularWorkflow(
    const HdMaterialNetwork2Interface &materialNetworkIface,
    TfToken usdPreviewSurfaceName)
{
  auto vtValue =
      materialNetworkIface.GetNodeParameterValue(usdPreviewSurfaceName,
          HdAnariMaterialTokens->useSpecularWorkflow);
  return vtValue.IsHolding<int>() ? vtValue.UncheckedGet<int>() : false;
}

void HdAnariUsdPreviewSurfaceConverter::ProcessUSDUVTexture(TfToken usdTextureName, TfToken outputName, const char* anariParameterName)
{
  if (auto it = samplerMapping_->find(SdfPath(usdTextureName).AppendProperty(outputName)); it != cend(*samplerMapping_)) {
    anari::setParameter(
      device_, material_, anariParameterName, it->second
    );

    // Gets the primvar linked to the st input.
    auto inputConnections = materialNetworkIface_->GetNodeInputConnection(usdTextureName, HdAnariMaterialTokens->st);
    if (inputConnections.empty()) return;

    if (inputConnections.size() > 1) {
      TF_DEBUG_MSG(HD_ANARI_MATERIAL,
          "Connected to more than 1 input. Skipping %s:%s\n",
          usdTextureName.GetText(),
          HdAnariMaterialTokens->st.GetText());
      return;
    }

    auto inputNodeName = inputConnections.front().upstreamNodeName;
    auto inputNodeType = materialNetworkIface_->GetNodeType(inputNodeName);


    if (inputNodeType == UsdImagingTokens->UsdPrimvarReader_float2) {
      // Then setup the anari input matching the connected primvar reader.
      ProcessPrimvarReader<GfVec2f>(inputNodeName, it->second, "inAttribute");
    }
  };
}

template <typename SamplerT>
void HdAnariUsdPreviewSurfaceConverter::ProcessPrimvarReader(
    TfToken usdPrimvarReaderName,
    anari::Object anariObject,
    const char *anariParameterName)
{
    if (auto itPrimvar = primvarMapping_->find(SdfPath(usdPrimvarReaderName)); itPrimvar != cend(*primvarMapping_)) {
      if (auto itBindingPoint = primvarBinding_->find(itPrimvar->second); itBindingPoint != cend(*primvarBinding_)) {
        auto bindingpoint = itBindingPoint->second;
        anari::setParameter(device_, anariObject, anariParameterName, bindingpoint.GetText());
        anari::commitParameters(device_, anariObject);
      }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
