// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "light.h"
#include "renderDelegate.h"
#include "renderParam.h"

#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/light.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/usd/usdLux/tokens.h>

#include <anari/anari_cpp.hpp>

#include <cmath>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Stage up axis used to align a dome light whose poleAxis is "scene".
TfToken ResolveDomePoleAxis(HdSceneDelegate *sceneDelegate, const SdfPath &id)
{
  auto poleAxis = sceneDelegate->GetLightParamValue(id, UsdLuxTokens->poleAxis)
                      .GetWithDefault<TfToken>(UsdLuxTokens->scene);
  if (poleAxis != UsdLuxTokens->scene)
    return poleAxis;

  auto *renderDelegate = sceneDelegate->GetRenderIndex().GetRenderDelegate();
  auto upAxis =
      renderDelegate->GetRenderSetting(HdAnariRenderSettingsTokens->upAxis);
  if (upAxis.IsHolding<TfToken>())
    return upAxis.UncheckedGet<TfToken>();
  if (upAxis.IsHolding<std::string>())
    return TfToken(upAxis.UncheckedGet<std::string>());
  return TfToken("Y");
}

// Rotation aligning the canonical Y-up latlong frame (pole +Y, center +Z, per
// the OpenEXR convention USD adopts) with the requested stage up axis. Applied
// to the hdri up/direction vectors so the instance transform stays the
// authored UsdGeomXformable alone.
GfMatrix4d DomePoleAlignment(const TfToken &poleAxis)
{
  if (poleAxis == TfToken("Z")) {
    // +90 about X: pole +Y->+Z, center +Z->-Y.
    return GfMatrix4d(1.0).SetRotate(GfRotation(GfVec3d::XAxis(), 90.0));
  }
  if (poleAxis != TfToken("Y"))
    TF_WARN("Unsupported dome pole axis '%s'; assuming Y.", poleAxis.GetText());
  return GfMatrix4d(1.0);
}

} // namespace

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

  const bool transformDirty = *dirtyBits & HdLight::DirtyTransform;
  const bool paramsDirty = *dirtyBits & HdLight::DirtyParams;
  const bool resourceDirty = *dirtyBits & HdLight::DirtyResource;

  // The instance carries only the authored UsdGeomXformable. A dome light's
  // pole-axis alignment is folded into its up/direction vectors below.
  if (transformDirty) {
    anari::setParameter(device_,
        instance_,
        "transform",
        GfMatrix4f(sceneDelegate->GetTransform(id)));
    anari::commitParameters(device_, instance_);
  }

  if (paramsDirty) {
    float intensity =
        sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity)
            .GetWithDefault<float>(1.0f);
    float exposure =
        sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure)
            .GetWithDefault<float>(0.0f);
    // UsdLux folds exposure into intensity as a power-of-two stop scale.
    float scaledIntensity = intensity * std::exp2(exposure);
    auto color = sceneDelegate->GetLightParamValue(id, HdLightTokens->color)
                     .GetWithDefault<GfVec3f>(GfVec3f(1.0f));
    // When set, UsdLux area lights hold total emitted power constant by
    // dividing the authored radiance by the emitter area.
    bool normalize =
        sceneDelegate->GetLightParamValue(id, HdLightTokens->normalize)
            .GetWithDefault<bool>(false);

    if (lightType_ == HdPrimTypeTokens->sphereLight) {
      float radius =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->radius)
              .GetWithDefault<float>(0.5f);
      anari::setParameter(device_, light_, "radius", radius);
      // Authored emission was previously dropped; the ANARI point light stayed
      // at its default. Map UsdLux intensity to radiant intensity (W/sr).
      // normalize holds total power constant by dividing by the emitter surface
      // area (4*pi*r^2), matching the rect/disk lights.
      float area = 4.0f * kPi * radius * radius;
      float intensity =
          (normalize && area > 0.0f) ? scaledIntensity / area : scaledIntensity;
      anari::setParameter(device_, light_, "color", color);
      anari::setParameter(device_, light_, "intensity", intensity);
    } else if (lightType_ == HdPrimTypeTokens->distantLight) {
      anari::setParameter(
          device_, light_, "direction", GfVec3f(0.0f, 0.0f, -1.0f));
      float angle = sceneDelegate->GetLightParamValue(id, HdLightTokens->angle)
                        .GetWithDefault<float>(0.53f);
      anari::setParameter(device_, light_, "angularDiameter", angle);
      // Authored intensity/exposure/color were previously dropped, leaving the
      // ANARI directional light at its default irradiance of 1 W/m^2.
      anari::setParameter(device_, light_, "color", color);
      anari::setParameter(device_, light_, "irradiance", scaledIntensity);
    } else if (lightType_ == HdPrimTypeTokens->domeLight) {
      // ANARI hdri up/direction are world-space directions the device rotates
      // by the instance transform, so they hold the latlong frame relative to
      // the authored xform. Align the canonical USD frame (pole +Y, center +Z)
      // to the stage up axis here; the instance carries only the xform.
      auto poleAlign =
          DomePoleAlignment(ResolveDomePoleAxis(sceneDelegate, id));
      anari::setParameter(device_,
          light_,
          "direction",
          GfVec3f(poleAlign.TransformDir(GfVec3d(0.0, 0.0, 1.0))));
      anari::setParameter(device_,
          light_,
          "up",
          GfVec3f(poleAlign.TransformDir(GfVec3d(0.0, 1.0, 0.0))));

      anari::setParameter(device_, light_, "color", color);
      anari::setParameter(device_, light_, "scale", scaledIntensity);
    } else if (lightType_ == HdPrimTypeTokens->rectLight) {
      auto width = sceneDelegate->GetLightParamValue(id, HdLightTokens->width)
                       .GetWithDefault<float>(1.0f);
      auto height = sceneDelegate->GetLightParamValue(id, HdLightTokens->height)
                        .GetWithDefault<float>(1.0f);
      // edge1 x edge2 is the emitting normal; order the full-width edges so it
      // points along local -Z to match UsdLux rect emission.
      auto edge1 = GfVec3f(width, 0.0f, 0.0f);
      auto edge2 = GfVec3f(0.0f, -height, 0.0f);
      anari::setParameter(device_,
          light_,
          "position",
          GfVec3f(-width / 2.0f, height / 2.0f, 0.0f));
      anari::setParameter(device_, light_, "edge1", edge1);
      anari::setParameter(device_, light_, "edge2", edge2);
      // UsdLux rect intensity is emitted radiance; normalize divides it by the
      // emitter area so total power is size-independent. The ANARI quad light's
      // emitted radiance is carried by "intensity" (VisRTX ignores "radiance").
      float area = width * height;
      float radiance =
          (normalize && area > 0.0f) ? scaledIntensity / area : scaledIntensity;
      anari::setParameter(device_, light_, "color", color);
      anari::setParameter(device_, light_, "intensity", radiance);

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
      }
    } else if (lightType_ == HdPrimTypeTokens->diskLight) {
      float radius =
          sceneDelegate->GetLightParamValue(id, HdLightTokens->radius)
              .GetWithDefault<float>(0.5f);
      anari::setParameter(device_, light_, "outerRadius", radius);
      // The ANARI ring light takes radiant intensity, not radiance. For a
      // Lambertian disk the on-axis radiant intensity is radiance * area.
      float area = kPi * radius * radius;
      float radiance =
          (normalize && area > 0.0f) ? scaledIntensity / area : scaledIntensity;
      anari::setParameter(device_, light_, "color", color);
      anari::setParameter(device_, light_, "intensity", radiance * area);
    }
    anari::commitParameters(device_, light_);
  }

  if (resourceDirty) {
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
              ANARI_FLOAT32_VEC3,
              /*flipVertical=*/true);
          anari::setAndReleaseParameter(device_, light_, "radiance", array);
        } else {
          // No resolvable HDRI: keep the uniform radiance set at construction
          // so the dome acts as a constant environment instead of aborting.
          TF_WARN(
              "No usable texture for dome light `%s`; "
              "falling back to uniform radiance",
              GetId().GetText());
        }
      } else {
        TF_WARN(
            "Unsupported hdri texture format `%s` for `%s`; "
            "falling back to uniform radiance",
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
      }
    }
    anari::commitParameters(device_, light_);
  }

  auto rp = static_cast<HdAnariRenderParam *>(renderParam);
  if (!registered_) {
    rp->RegisterLight(this);
    registered_ = true;
  }

  // Any edit -- including an upAxis-driven re-orient the render pass pushes via
  // DirtyParams -- must publish a new scene version, or the render pass won't
  // re-commit the world and the device keeps the stale, converged frame.
  if (transformDirty || paramsDirty || resourceDirty)
    rp->MarkNewSceneVersion();

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