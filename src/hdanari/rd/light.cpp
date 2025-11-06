// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "light.h"
#include "renderParam.h"

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/usd/usdLux/tokens.h>

#include <anari/anari_cpp.hpp>

PXR_NAMESPACE_OPEN_SCOPE

HdAnariLight::HdAnariLight(
    anari::Device device, SdfPath const &id, TfToken const &lightType)
    : HdLight(id), device_(device), lightType_(lightType)
{
  if (id.IsEmpty())
    return;

  if (lightType == HdPrimTypeTokens->distantLight) {
    light_ = anari::newObject<anari::Light>(device, "directional");
  } else if (lightType == HdPrimTypeTokens->domeLight) {
    light_ = anari::newObject<anari::Light>(device, "hdri");
    auto white = GfVec3f(1.0f);
    anari::setParameterArray2D(device, light_, "radiance", &white, 1, 1);
  } else if (lightType == HdPrimTypeTokens->rectLight) {
    light_ = anari::newObject<anari::Light>(device, "quad");
  } else if (lightType == HdPrimTypeTokens->sphereLight
      || lightType == HdPrimTypeTokens->simpleLight) {
    light_ = anari::newObject<anari::Light>(device, "point");
  } else if (lightType == HdPrimTypeTokens->diskLight) {
    light_ = anari::newObject<anari::Light>(device, "ring");
  }
  assert(light_);

  anari::commitParameters(device, light_);
  auto group = anari::newObject<anari::Group>(device);
  anari::setParameterArray1D(device, group, "light", &light_, 1);
  anari::commitParameters(device, group);
  instance_ = anari::newObject<anari::Instance>(device, "transform");
  anari::setAndReleaseParameter(device, instance_, "group", group);
  anari::commitParameters(device, instance_);
}

HdAnariLight::~HdAnariLight() = default;

void HdAnariLight::Sync(HdSceneDelegate *sceneDelegate,
    HdRenderParam *renderParam,
    HdDirtyBits *dirtyBits)
{
  auto id = GetId();
  if (id.IsEmpty())
    return;

  if (HdLight::DirtyTransform && *dirtyBits) {
    auto transform = sceneDelegate->GetTransform(id);
    anari::setParameter(device_, instance_, "transform", GfMatrix4f(transform));
    anari::commitParameters(device_, instance_);
  }

  if (HdLight::DirtyParams && *dirtyBits) {
    float intensity =
        sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity)
            .GetWithDefault<float>(1.0f);
    bool normalize =
        sceneDelegate->GetLightParamValue(id, HdLightTokens->normalize)
            .GetWithDefault<bool>(false);

    if (lightType_ == HdPrimTypeTokens->sphereLight) {
      float radius =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->radius)
              .GetWithDefault<float>(0.0f);
      anari::setParameter(device_, light_, "radius", radius);
    } else if (lightType_ == HdPrimTypeTokens->distantLight) {
      anari::setParameter(
          device_, light_, "direction", GfVec3f(0.0f, 0.0f, -1.0f));
      float angle = sceneDelegate->GetLightParamValue(id, HdLightTokens->angle)
                        .GetWithDefault<float>(0.53f);
      anari::setParameter(device_, light_, "angularDiameter", angle);
    } else if (lightType_ == HdPrimTypeTokens->domeLight) {
      auto poleAxis =
          sceneDelegate->GetLightParamValue(id, UsdLuxTokens->poleAxis)
              .GetWithDefault<TfToken>(UsdLuxTokens->scene);
      if (poleAxis == UsdLuxTokens->scene) {
        // Assume default Y up. Might be false if the scene up axis was changed.
        poleAxis = TfToken("Y");
      }

      if (poleAxis == TfToken("Y")) {
        anari::setParameter(
            device_, light_, "direction", GfVec3f(0.0f, 0.0f, 1.0f));
        anari::setParameter(device_, light_, "up", GfVec3f(0.0f, 1.0f, 0.0f));
      } else if (poleAxis == TfToken("Z")) {
        anari::setParameter(
            device_, light_, "direction", GfVec3f(1.0f, 0.0f, 0.0f));
        anari::setParameter(device_, light_, "up", GfVec3f(0.0f, 0.0f, 1.0f));
      } else {
        TF_RUNTIME_ERROR("Unknown up axis %s for %s. Assuming Y.",
            poleAxis.GetText(),
            GetId().GetText());
        anari::setParameter(device_, light_, "direction", GfVec3f(0, 0, -1));
      }

      auto intensity =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity)
              .GetWithDefault<float>(1.0f);
      anari::setParameter(device_, light_, "scale", intensity);
    } else if (lightType_ == HdPrimTypeTokens->rectLight) {
      auto width = sceneDelegate->GetLightParamValue(id, HdLightTokens->width)
                       .GetWithDefault<float>(1.0f);
      auto height = sceneDelegate->GetLightParamValue(id, HdLightTokens->height)
                        .GetWithDefault<float>(1.0f);
      auto edge1 = GfVec3f(width / 2.0f, 0.0f, 0.0f);
      auto edge2 = GfVec3f(0.0f, height / 2.0f, 0.0f);
      anari::setParameter(device_,
          light_,
          "position",
          GfVec3f(-width / 2.0f, -height / 2.0f, 0.0f));
      anari::setParameter(device_, light_, "edge1", edge1);
      anari::setParameter(device_, light_, "edge2", edge2);

      auto textureFile =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile)
              .GetWithDefault<SdfAssetPath>(SdfAssetPath());
      if (!textureFile.GetResolvedPath().empty()) {
        auto array = HdAnariTextureLoader::LoadHioTexture2D(device_,
            textureFile.GetResolvedPath(),
            HdAnariTextureLoader::MinMagFilter::Linear,
            HdAnariTextureLoader::ColorSpace::Raw);

        anari::setAndReleaseParameter(
            device_, light_, "intensityDistribution", array);
      } else {
        TF_RUNTIME_ERROR("Failed to open texture file `%s` for `%s`, ignoring",
            textureFile.GetResolvedPath().c_str(),
            GetId().GetText());
      }
    } else if (lightType_ == HdPrimTypeTokens->diskLight) {
      float radius =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->radius)
              .GetWithDefault<float>(0.0f);
      anari::setParameter(device_, light_, "outerRadius", radius);
    }
    anari::commitParameters(device_, light_);
  }

  if (HdLight::DirtyResource && *dirtyBits) {
    if (lightType_ == HdPrimTypeTokens->domeLight) {
      auto textureFormat =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFormat);
      if (textureFormat.IsEmpty()) {
        textureFormat = sceneDelegate->Get(id, HdLightTokens->textureFormat);
      }
      auto textureFormatTk =
          textureFormat.GetWithDefault<TfToken>(UsdLuxTokens->automatic);
      if (textureFormatTk == UsdLuxTokens->latlong
          || textureFormatTk == UsdLuxTokens->automatic) {
        auto textureFile =
            sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
        if (textureFile.IsEmpty()) {
          textureFile = sceneDelegate->Get(id, HdLightTokens->textureFile);
        }
        auto textureFilePath =
            textureFile.GetWithDefault<SdfAssetPath>(SdfAssetPath());
        if (!textureFilePath.GetResolvedPath().empty()) {
          auto array = HdAnariTextureLoader::LoadHioTexture2D(device_,
              textureFilePath.GetResolvedPath(),
              HdAnariTextureLoader::MinMagFilter::Linear,
              HdAnariTextureLoader::ColorSpace::Raw,
              ANARI_FLOAT32_VEC3);
          anari::setAndReleaseParameter(device_, light_, "radiance", array);
        } else {
          TF_RUNTIME_ERROR(
              "Failed to open texture file `%s` for `%s`, ignoring",
              textureFilePath.GetResolvedPath().c_str(),
              GetId().GetText());
        }
      } else {
        TF_RUNTIME_ERROR("Unsupported hdri texture format `%s` for `%s`",
            textureFormatTk.GetText(),
            GetId().GetText());
      }
    } else if (lightType_ == HdPrimTypeTokens->rectLight) {
      auto textureFile =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile)
              .GetWithDefault<SdfAssetPath>(SdfAssetPath());
      if (!textureFile.GetResolvedPath().empty()) {
        auto array = HdAnariTextureLoader::LoadHioTexture2D(device_,
            textureFile.GetResolvedPath(),
            HdAnariTextureLoader::MinMagFilter::Linear,
            HdAnariTextureLoader::ColorSpace::Raw);

        anari::setAndReleaseParameter(
            device_, light_, "intensityDistribution", array);
      } else {
        TF_RUNTIME_ERROR("Failed to open texture file `%s` for `%s`, ignoring",
            textureFile.GetResolvedPath().c_str(),
            GetId().GetText());
      }
    }
    anari::commitParameters(device_, light_);
  }

  if (!registered_) {
    auto rp = static_cast<HdAnariRenderParam *>(renderParam);
    rp->RegisterLight(this);
    rp->MarkNewSceneVersion();

    registered_ = true;
  }

  *dirtyBits = HdLight::Clean;
}

HdDirtyBits HdAnariLight::GetInitialDirtyBitsMask() const
{
  // Implementation of GetInitialDirtyBitsMask function
  return HdLight::DirtyBits::AllDirty;
}

void HdAnariLight::Finalize(HdRenderParam *renderParam)
{
  auto rp = static_cast<HdAnariRenderParam *>(renderParam);
  rp->UnregisterLight(this);
  rp->MarkNewSceneVersion();

  anari::release(device_, light_);
  anari::release(device_, instance_);
}

PXR_NAMESPACE_CLOSE_SCOPE