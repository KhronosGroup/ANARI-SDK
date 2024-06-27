#include "usdPreviewSurface.h"

#include "../hdAnariTypes.h"
#include "../debugCodes.h"

#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/pxr.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/usd/sdf/assetPath.h>

#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(HdAnariUsdPreviewSurfaceToken,
  (a)
  (b)
  (file)
  (g)
  (opacityThreshold)
  (r)
  (st)
  (useSpecularWorkflow)
);

static anari::DataType HioFormatToAnari(HioFormat f)
{
  switch (f) {
  case HioFormatUNorm8:
    return ANARI_UFIXED8;
  case HioFormatUNorm8Vec2:
    return ANARI_UFIXED8_VEC2;
  case HioFormatUNorm8Vec3:
    return ANARI_UFIXED8_VEC3;
  case HioFormatUNorm8Vec4:
    return ANARI_UFIXED8_VEC4;
  case HioFormatSNorm8:
    return ANARI_FIXED8;
  case HioFormatSNorm8Vec2:
    return ANARI_FIXED8_VEC2;
  case HioFormatSNorm8Vec3:
    return ANARI_FIXED8_VEC3;
  case HioFormatSNorm8Vec4:
    return ANARI_FIXED8_VEC4;
  case HioFormatFloat16:
    return ANARI_FLOAT16;
  case HioFormatFloat16Vec2:
    return ANARI_FLOAT16_VEC2;
  case HioFormatFloat16Vec3:
    return ANARI_FLOAT16_VEC3;
  case HioFormatFloat16Vec4:
    return ANARI_FLOAT16_VEC4;
  case HioFormatFloat32:
    return ANARI_FLOAT32;
  case HioFormatFloat32Vec2:
    return ANARI_FLOAT32_VEC2;
  case HioFormatFloat32Vec3:
    return ANARI_FLOAT32_VEC3;
  case HioFormatFloat32Vec4:
    return ANARI_FLOAT32_VEC4;
  case HioFormatDouble64:
    return ANARI_FLOAT64;
  case HioFormatDouble64Vec2:
    return ANARI_FLOAT64_VEC2;
  case HioFormatDouble64Vec3:
    return ANARI_FLOAT64_VEC3;
  case HioFormatDouble64Vec4:
    return ANARI_FLOAT64_VEC4;
  case HioFormatUInt16:
    return ANARI_UINT16;
  case HioFormatUInt16Vec2:
    return ANARI_UINT16_VEC2;
  case HioFormatUInt16Vec3:
    return ANARI_UINT16_VEC3;
  case HioFormatUInt16Vec4:
    return ANARI_UINT16_VEC4;
  case HioFormatInt16:
    return ANARI_INT16;
  case HioFormatInt16Vec2:
    return ANARI_INT16_VEC2;
  case HioFormatInt16Vec3:
    return ANARI_INT16_VEC3;
  case HioFormatInt16Vec4:
    return ANARI_INT16_VEC4;
  case HioFormatUInt32:
    return ANARI_UINT32;
  case HioFormatUInt32Vec2:
    return ANARI_UINT32_VEC2;
  case HioFormatUInt32Vec3:
    return ANARI_UINT32_VEC3;
  case HioFormatUInt32Vec4:
    return ANARI_UINT32_VEC4;
  case HioFormatInt32:
    return ANARI_INT32;
  case HioFormatInt32Vec2:
    return ANARI_INT32_VEC2;
  case HioFormatInt32Vec3:
    return ANARI_INT32_VEC3;
  case HioFormatInt32Vec4:
    return ANARI_INT32_VEC4;
  case HioFormatUNorm8srgb:
    return ANARI_UFIXED8_R_SRGB;
  case HioFormatUNorm8Vec2srgb:
    return ANARI_UFIXED8_RA_SRGB;
  case HioFormatUNorm8Vec3srgb:
    return ANARI_UFIXED8_RGB_SRGB;
  case HioFormatUNorm8Vec4srgb:
    return ANARI_UFIXED8_RGBA_SRGB;
  default: // all other values, including HioFormatInvalid
    break;
  }

  return ANARI_UNKNOWN;
}

static anari::Sampler LoadHioTexture2D(anari::Device d,
    const std::string &file,
    bool nearestFilter)
{
  const auto image = HioImage::OpenForReading(file);
  if (!image) {
    TF_WARN("failed to open texture file '%s'\n", file.c_str());
    return {};
  }

  HioImage::StorageSpec desc;
  desc.format = image->GetFormat();
  desc.width = image->GetWidth();
  desc.height = image->GetHeight();
  desc.depth = 1;
  desc.flipped = true;

  auto format = HioFormatToAnari(desc.format);
  if (format == ANARI_UNKNOWN) {
    TF_WARN("failed to load texture '%s' due to unsupported format\n",
        file.c_str());
    return {};
  }

  auto array = anari::newArray2D(d, format, desc.width, desc.height);
  desc.data = anari::map<void>(d, array);

  bool loaded = image->Read(desc);
  if (!loaded) {
    TF_WARN("failed to load texture '%s'\n", file.c_str());
    anari::unmap(d, array);
    anari::release(d, array);
    return {};
  }

  anari::unmap(d, array);

  auto texture = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setParameter(d, texture, "image", array);
  anari::setParameter(d, texture, "filter", nearestFilter ? "nearest" : "linear");
  anari::setParameter(d, texture, "inAttribute", "attribute0");

  anari::release(d, array);

  return texture;
}


bool HdAnariUsdPreviewSurfaceConverter::ProcessConnection(const HdMaterialNetwork2Interface& materialNetworkIface,
    TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName
) {
  auto inputConnections = materialNetworkIface.GetNodeInputConnection(usdPreviewSurfaceName, usdInputName);
  if (inputConnections.empty()) return false;

  if (inputConnections.size() > 1) {
      TF_DEBUG_MSG(HD_ANARI_MATERIAL, "Connected to more than 1 input. Skipping %s:%s\n", usdPreviewSurfaceName.GetText(), usdInputName.GetText());
  }

  auto inputNodeName = inputConnections.front().upstreamNodeName;
  auto inputNodeType = materialNetworkIface.GetNodeType(inputNodeName);

  if (inputNodeType == UsdImagingTokens->UsdUVTexture) {
      auto outputName = inputConnections.front().upstreamOutputName;
      ProcessUSDUVTexture(materialNetworkIface, inputNodeName, outputName, anariParameterName);
  } else {
      TF_WARN("Unsupported input type %s for %s:%s\n", inputNodeType.GetText(), usdPreviewSurfaceName.GetText(), usdInputName.GetText());
      return false;
  }

  return true;
}

bool HdAnariUsdPreviewSurfaceConverter::ProcessValue(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName, const char* anariParameterName) {
    auto value = materialNetworkIface.GetNodeParameterValue(usdPreviewSurfaceName, usdInputName);
    if (value.GetType().IsA<float>()) {
        anari::setParameter(_device, _material, anariParameterName, value.Get<float>());
    } else if (value.GetType().IsA<GfVec2f>()) {
        anari::setParameter(_device, _material, anariParameterName, value.Get<GfVec2f>());
    }  else if (value.GetType().IsA<GfVec3f>()) {
        anari::setParameter(_device, _material, anariParameterName, value.Get<GfVec3f>());
    }  else if (value.GetType().IsA<GfVec4f>()) {
        anari::setParameter(_device, _material, anariParameterName, value.Get<GfVec4f>());
    } else if (value.GetType().IsA<void>()) {
      return false; // Special case for unassigned values. Consider them unauthored, instead of erroring out about void not being a valid type.
    } else {
        TF_WARN("Unsupported value type %s at %s:%s\n", value.GetTypeName().c_str(), usdPreviewSurfaceName.GetText(), usdInputName.GetText());
        return false;
    }

    return true;
}

HdAnariUsdPreviewSurfaceConverter::AlphaMode HdAnariUsdPreviewSurfaceConverter::GetAlphaMode(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName) const {
  AlphaMode alphaMode = AlphaMode::Blend;

  auto vtOpacityThreshold = materialNetworkIface.GetNodeParameterValue(usdPreviewSurfaceName, HdAnariUsdPreviewSurfaceToken->opacityThreshold);
  if (vtOpacityThreshold.IsHolding<float>()) {
    float opacityThreshold = vtOpacityThreshold.UncheckedGet<float>();
    if (opacityThreshold > 0.0f) {
      alphaMode = AlphaMode::Mask;
    }
  } else {
    auto vtOpacity = materialNetworkIface.GetNodeParameterValue(usdPreviewSurfaceName, HdAnariUsdPreviewSurfaceToken->opacityThreshold);
    if (vtOpacity.IsHolding<float>()) {
      float opacity  = vtOpacity.UncheckedGet<float>();
      if (opacity == 1.0f) {
        alphaMode = AlphaMode::Opaque;
       }
    }
  }

  return alphaMode;
}

bool HdAnariUsdPreviewSurfaceConverter::UseSpecularWorkflow(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName) const {
  auto vtValue = materialNetworkIface.GetNodeParameterValue(usdPreviewSurfaceName, HdAnariUsdPreviewSurfaceToken->useSpecularWorkflow);
  return vtValue.IsHolding<int>() ? vtValue.UncheckedGet<int>() : false;
}


// https://openusd.org/release/spec_usdpreviewsurface.html#texture-reader
void HdAnariUsdPreviewSurfaceConverter::ProcessUSDUVTexture(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdTextureName, TfToken outputName, const char* anariParameterName) {

    // File name
    auto file = materialNetworkIface.GetNodeParameterValue(usdTextureName, HdAnariUsdPreviewSurfaceToken->file);
    auto assetPath = file.Get<SdfAssetPath>().GetResolvedPath();
    auto sampler = LoadHioTexture2D(_device, assetPath, false);

    // If connnected to a specific channel, swizzle the color components to
    // mimic the connection behavior
    if (outputName == HdAnariUsdPreviewSurfaceToken->g) {
      static const constexpr float tx[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
      };
      anari::setParameter(_device, sampler, "outTransform", ANARI_FLOAT32_MAT4, tx);
    } else if (outputName == HdAnariUsdPreviewSurfaceToken->g) {
      static const constexpr float tx[] = {
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
      };
      anari::setParameter(_device, sampler, "outTransform", ANARI_FLOAT32_MAT4, tx);
    } else if (outputName == HdAnariUsdPreviewSurfaceToken->b) {
      static const constexpr float tx[] = {
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
      };
      anari::setParameter(_device, sampler, "outTransform", ANARI_FLOAT32_MAT4, tx);
    }  else if (outputName == HdAnariUsdPreviewSurfaceToken->a) {
      static const constexpr float tx[] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
      };
      anari::setParameter(_device, sampler, "outTransform", ANARI_FLOAT32_MAT4, tx);
    } // nothing to do for non swizzled data or just accessing the r channel

    anari::commitParameters(_device, sampler);

    // Texture Coordinates. Ignored for now. Assumes to be in attribute0 of the supporting mesh.
    // Should be checking for a connection an PrimvarReader of of Float2 and act from there.

    anari::setParameter(_device, _material, anariParameterName, sampler);
    anari::commitParameters(_device, _material);
    anari::release(_device, sampler);
}

void HdAnariUsdPreviewSurfaceConverter::ProcessUsdPrimvarReader(const HdMaterialNetwork2Interface& materialNetworkIface, TfToken usdPreviewSurfaceName, TfToken usdInputName,
    const char* anariParameterName,
    ANARIDataType anariDataType) {

}

PXR_NAMESPACE_CLOSE_SCOPE
