// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "material.h"
#include "renderParam.h"
// pxr
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hio/image.h>

PXR_NAMESPACE_OPEN_SCOPE

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(HdAnariMaterialTokens,
  (bias)
  (diffuseColor)
  (fallback)
  (file)
  (filename)
  (infoFilename)
  (opacity)
  (scale)
  (sourceColorSpace)
  (st)
  (UsdPreviewSurface)
  (UsdUVTexture)
  (wrapS)
  (wrapT)
);

static std::map<TfToken, std::string> s_previewMatNames = {
  {HdAnariMaterialTokens->diffuseColor, "color"},
  {HdAnariMaterialTokens->opacity, "opacity"}
};

static std::map<TfToken, anari::DataType> s_previewMatTypes = {
  {HdAnariMaterialTokens->diffuseColor, ANARI_FLOAT32_VEC3},
  {HdAnariMaterialTokens->opacity, ANARI_FLOAT32}
};
// clang-format on

// Helper functions ///////////////////////////////////////////////////////////

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
    const std::string channelsStr,
    bool nearestFilter,
    bool complement)
{
  const auto image = HioImage::OpenForReading(file);
  if (!image) {
    TF_DEBUG_MSG(ANARI, "failed to open texture file '%s'\n", file.c_str());
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
    TF_DEBUG_MSG(ANARI,
        "failed to load texture '%s' due to unsupported format\n",
        file.c_str());
    return {};
  }

  auto array = anari::newArray2D(d, format, desc.width, desc.height);
  desc.data = anari::map<void>(d, array);

  bool loaded = image->Read(desc);
  if (!loaded) {
    TF_DEBUG_MSG(ANARI, "failed to load texture '%s'\n", file.c_str());
    anari::unmap(d, array);
    anari::release(d, array);
    return {};
  }

  anari::unmap(d, array);

  auto texture = anari::newObject<anari::Sampler>(d, "image2D");
  anari::setParameter(d, texture, "image", array);
  anari::setParameter(
      d, texture, "filter", nearestFilter ? "nearest" : "linear");
  anari::setParameter(d, texture, "inAttribute", "attribute0");

  anari::release(d, array);

  return texture;
}

// HdAnariMaterial ////////////////////////////////////////////////////////////

HdAnariMaterial::HdAnariMaterial(anari::Device d, const SdfPath &id)
    : HdMaterial(id)
{
  if (!d)
    return;

  anari::retain(d, d);

  _anari.device = d;
  _anari.material = anari::newObject<anari::Material>(d, "matte");
  anari::setParameter(d, _anari.material, "alphaMode", "blend");
  anari::setParameter(d, _anari.material, "color", "color");
  anari::commitParameters(d, _anari.material);
}

HdAnariMaterial::~HdAnariMaterial()
{
  if (!_anari.device)
    return;

  for (auto &t : _anari.textures)
    anari::release(_anari.device, t.second);
  anari::release(_anari.device, _anari.material);
  anari::release(_anari.device, _anari.device);
}

void HdAnariMaterial::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  if (*dirtyBits & HdMaterial::DirtyResource) {
    //  find material network
    VtValue networkMapResource = sceneDelegate->GetMaterialResource(GetId());
    HdMaterialNetworkMap networkMap =
        networkMapResource.Get<HdMaterialNetworkMap>();
    HdMaterialNetwork matNetwork;

    TF_FOR_ALL(itr, networkMap.map)
    {
      auto &network = itr->second;
      TF_FOR_ALL(node, network.nodes)
      {
        if (node->identifier == HdAnariMaterialTokens->UsdPreviewSurface)
          matNetwork = network;
      }
    }

    // process each material node based on type
    TF_FOR_ALL(node, matNetwork.nodes)
    {
      if (node->identifier == HdAnariMaterialTokens->UsdPreviewSurface)
        _ProcessUsdPreviewSurfaceNode(*node);
      else if (node->identifier == HdAnariMaterialTokens->UsdUVTexture) {
        auto relationships = matNetwork.relationships;
        auto relationship = std::find_if(relationships.begin(),
            relationships.end(),
            [&node](HdMaterialRelationship const &rel) {
              return rel.inputId == node->path;
            });

        if (relationship == relationships.end())
          continue;

        const TfToken inputNameToken = relationship->inputName;
        const TfToken texNameToken = relationship->outputName;
        _ProcessTextureNode(*node, inputNameToken, texNameToken);
#if 0
      } else if (node->identifier == HdAnariMaterialTokens->UsdTransform2d) {
        // calculate transform2d to be used on a texture
        auto relationships = matNetwork.relationships;
        auto relationship = std::find_if(relationships.begin(),
            relationships.end(),
            [&node](HdMaterialRelationship const &rel) {
              return rel.inputId == node->path;
            });
        if (relationship == relationships.end()) {
          continue; // skip unused nodes
        }

        TfToken texNameToken = relationship->outputName;

        // node is used, find what param it representds, ie diffuseColor
        auto relationship2 = std::find_if(relationships.begin(),
            relationships.end(),
            [&relationship](HdMaterialRelationship const &rel) {
              return rel.inputId == relationship->outputId;
            });
        if (relationship2 == relationships.end()) {
          continue; // skip unused nodes
        }
        _ProcessTransform2dNode(*node, relationship2->outputName);
#endif
      }
    }

    *dirtyBits = Clean;
  }
}

HdDirtyBits HdAnariMaterial::GetInitialDirtyBitsMask() const
{
  return AllDirty;
}

anari::Material HdAnariMaterial::GetANARIMaterial() const
{
  return _anari.material;
}

void HdAnariMaterial::_ProcessUsdPreviewSurfaceNode(HdMaterialNode node)
{
  auto d = _anari.device;
  auto m = _anari.material;

  TF_FOR_ALL(param, node.parameters)
  {
    const auto &name = param->first;
    const auto &value = param->second;
    if (name == HdAnariMaterialTokens->diffuseColor)
      anari::setParameter(d, m, "color", value.Get<GfVec3f>());
    else if (name == HdAnariMaterialTokens->opacity)
      anari::setParameter(d, m, "opacity", value.Get<float>());
#if 0
    else if (name == HdAnariMaterialTokens->specularColor)
      specularColor = value.Get<GfVec3f>();
    else if (name == HdAnariMaterialTokens->metallic)
      metallic = value.Get<float>();
    else if (name == HdAnariMaterialTokens->roughness)
      roughness = value.Get<float>();
    else if (name == HdAnariMaterialTokens->ior)
      ior = value.Get<float>();
    else if (name == HdAnariMaterialTokens->clearcoatRoughness)
      coatRoughness = value.Get<float>();
    else if (name == HdAnariMaterialTokens->clearcoat)
      coat = value.Get<float>();
#endif
  }
#if 0
  // set transmissionColor to diffuseColor for usdpreviewsurface as there is
  // no way to set it
  transmissionColor = diffuseColor;
#endif

  anari::commitParameters(d, m);
}

void HdAnariMaterial::_ProcessTextureNode(
    HdMaterialNode node, const TfToken &inputName, const TfToken &outputName)
{
  anari::Sampler &texture = _anari.textures[outputName];
  std::string filename;
  TF_FOR_ALL(param, node.parameters)
  {
    const auto &name = param->first;
    const auto &value = param->second;
    if (name == HdAnariMaterialTokens->file
        || name == HdAnariMaterialTokens->filename
        || name == HdAnariMaterialTokens->infoFilename) {
      SdfAssetPath const &path = value.Get<SdfAssetPath>();
      filename = path.GetResolvedPath();
    } else if (name == HdAnariMaterialTokens->scale) {
#if 0
      texture.scale = value.Get<GfVec4f>();
#endif
    } else if (name == HdAnariMaterialTokens->wrapS) {
    } else if (name == HdAnariMaterialTokens->wrapT) {
    } else if (name == HdAnariMaterialTokens->st) {
    } else if (name == HdAnariMaterialTokens->bias) {
    } else if (name == HdAnariMaterialTokens->fallback) {
#if 0
      fallback = value.Get<GfVec4f>();
#endif
    } else if (name == HdAnariMaterialTokens->sourceColorSpace) {
    }
  }

  if (filename.empty() || TfStringContains(filename, "<UDIM>"))
    return;

  texture = LoadHioTexture2D(_anari.device,
      filename,
      inputName.GetString(),
      false,
      outputName == HdAnariMaterialTokens->opacity);

  anari::setParameter(_anari.device,
      _anari.material,
      s_previewMatNames[outputName].c_str(),
      texture);
  anari::commitParameters(_anari.device, _anari.material);
}

PXR_NAMESPACE_CLOSE_SCOPE