// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"
#include "textureLoader.h"

#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hio/types.h>
#include <pxr/pxr.h>

#include <anari/anari_cpp.hpp>

#include <set>

PXR_NAMESPACE_OPEN_SCOPE

class HdAnariUsdPreviewSurfaceConverter
{
  static const std::set<TfToken> inputIsSrgb;

 public:
  enum class AlphaMode
  {
    Opaque,
    Blend,
    Mask,
  };

  static HdAnariMaterial::TextureDescMapping EnumerateTextures(
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken usdPreviewSurfaceNodeName);
  static HdAnariMaterial::PrimvarMapping EnumeratePrimvars(
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken usdPreviewSurfaceName);

  HdAnariUsdPreviewSurfaceConverter(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface *materialNetworkIface,
      const TfToken &terminalNode,
      const HdAnariMaterial::PrimvarBinding *primvarBinding,
      const HdAnariMaterial::PrimvarMapping *primvarMapping,
      const HdAnariMaterial::SamplerMapping *samplerMapping);

  void ProcessInputConnection(TfToken usdInputName,
      const char *anariParameterName,
      HdAnariTextureLoader::ColorSpace fallbackColorspace =
          HdAnariTextureLoader::ColorSpace::SRgb)
  {
    if (!ProcessConnection(
            usdInputName, anariParameterName, fallbackColorspace)) {
      anari::unsetParameter(device_, material_, anariParameterName);
    }
  }

  template <typename T>
  void ProcessInputValue(TfToken usdInputName,
      const char *anariParameterName,
      T defaultValue,
      HdAnariTextureLoader::ColorSpace fallbackColorspace =
          HdAnariTextureLoader::ColorSpace::SRgb)
  {
    if (ProcessValue(usdInputName, anariParameterName, fallbackColorspace))
      return;

    if constexpr (std::is_void<T>()) {
      anari::unsetParameter(device_, material_, anariParameterName);
    } else {
      anari::setParameter(device_, material_, anariParameterName, defaultValue);
    }
  }

  template <typename T>
  void ProcessInput(TfToken usdInputName,
      const char *anariParameterName,
      T defaultValue,
      HdAnariTextureLoader::ColorSpace fallbackColorspace =
          HdAnariTextureLoader::ColorSpace::SRgb)
  {
    if (ProcessConnection(usdInputName, anariParameterName, fallbackColorspace))
      return;
    if (ProcessValue(usdInputName, anariParameterName, fallbackColorspace))
      return;

    if constexpr (std::is_void<T>()) {
      anari::unsetParameter(device_, material_, anariParameterName);
    } else {
      anari::setParameter(device_, material_, anariParameterName, defaultValue);
    }
  }

  static bool UseSpecularWorkflow(
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken usdPreviewSurfaceName);
  static AlphaMode GetAlphaMode(
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken usdPreviewSurfaceName);

 private:
  anari::Device device_;
  anari::Material material_;
  const HdMaterialNetwork2Interface *materialNetworkIface_;
  TfToken terminalNode_;

  const HdAnariMaterial::PrimvarBinding *primvarBinding_;
  const HdAnariMaterial::PrimvarMapping *primvarMapping_;
  const HdAnariMaterial::SamplerMapping *samplerMapping_;

  bool ProcessConnection(TfToken usdInputName,
      const char *anariParameterName,
      HdAnariTextureLoader::ColorSpace fallbackColorspace =
          HdAnariTextureLoader::ColorSpace::SRgb);
  bool ProcessValue(TfToken usdInputName,
      const char *anariParameterName,
      HdAnariTextureLoader::ColorSpace fallbackColorspace =
          HdAnariTextureLoader::ColorSpace::SRgb);

  void ProcessUSDUVTexture(TfToken usdTextureName,
      TfToken outputName,
      const char *anariParameterName);

  void ProcessUsdPrimvarReader(TfToken usdInputName,
      const char *anariParameterName,
      ANARIDataType anariDataType);

  template <typename SamplerT>
  void ProcessPrimvarReader(TfToken usdPrimvarReaderName,
      anari::Object anariObject,
      const char *anariParameterName);
};

PXR_NAMESPACE_CLOSE_SCOPE
