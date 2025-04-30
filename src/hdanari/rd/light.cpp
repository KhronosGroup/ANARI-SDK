// filepath: /var/home/tarcila/Code/ANARI/hdanari/mdl/src/hdanari/rd/light.cpp

#include "light.h"
#include "renderParam.h"

#include <pxr/imaging/hd/types.h>

#include <anari/anari_cpp.hpp>

PXR_NAMESPACE_OPEN_SCOPE

HdAnariLight::HdAnariLight(anari::Device device, SdfPath const& id, TfToken const& lightType)
    : HdLight(id), device_(device), lightType_(lightType)
{
  if (lightType == HdPrimTypeTokens->distantLight) {
    light_ = anari::newObject<anari::Light>(device, "directional");
  } else if (lightType == HdPrimTypeTokens->domeLight) {
    light_ = anari::newObject<anari::Light>(device, "hdri");
    auto white = GfVec3f(1.0f);
    anari::setParameterArray2D(device, light_, "radiance",
      &white, 1, 1);
  } else if (lightType == HdPrimTypeTokens->rectLight) {
    light_ = anari::newObject<anari::Light>(device, "quad");
  } else if (lightType == HdPrimTypeTokens->sphereLight) {
    light_ = anari::newObject<anari::Light>(device, "point");
  }

  anari::commitParameters(device, light_);
  auto group = anari::newObject<anari::Group>(device);
  anari::setParameterArray1D(device, group, "light", &light_, 1);
  anari::commitParameters(device, group);
  instance_ = anari::newObject<anari::Instance>(device, "transform");
  anari::setAndReleaseParameter(device, instance_, "group", group);
  anari::commitParameters(device, instance_);
  
}

HdAnariLight::~HdAnariLight() = default;

void HdAnariLight::Sync(HdSceneDelegate* sceneDelegate,
                        HdRenderParam* renderParam,
                        HdDirtyBits* dirtyBits)
{
  auto id = GetId();

  if (HdLight::DirtyTransform && *dirtyBits)
  {
    // Special case here. Our reference renderer VisRTX does not yet support light transform
    // so we handle a few case by transforming the light itself instead of the instance.
    auto transform = sceneDelegate->GetTransform(id);
    if (lightType_ == HdPrimTypeTokens->distantLight) {
      auto defaultLightDirection = GfVec3f(0.0f, 0.0f, -1.0f);
      auto lightDirection = transform.TransformDir(defaultLightDirection);
      anari::setParameter(device_, light_, "direction", GfVec3f(lightDirection));
      anari::commitParameters(device_, light_);
    } else if (lightType_ == HdPrimTypeTokens->sphereLight) {
      auto lightPosition = transform.ExtractTranslation();
      anari::setParameter(device_, light_, "position", GfVec3f(lightPosition));
      anari::commitParameters(device_, light_);
    } else if (lightType_ == HdPrimTypeTokens->domeLight) {
      auto defaultLightDirection = GfVec3f(1.0f, 0.0f, 0.0f);
      auto lightDirection = transform.TransformDir(defaultLightDirection);
      anari::setParameter(device_, light_, "direction", GfVec3f(lightDirection));
      anari::commitParameters(device_, light_);
    } else if (lightType_ == HdPrimTypeTokens->rectLight) {
      auto lightPosition = transform.ExtractTranslation();
      anari::setParameter(device_, light_, "position", GfVec3f(lightPosition));

      auto edge1 = GfVec3f(1.0f, 0.0f, 0.0f);
      auto edge2 = GfVec3f(0.0f, 1.0f, 0.0f); 
      edge1 = GfVec3f(transform.TransformDir(edge1));
      edge2 = GfVec3f(transform.TransformDir(edge2));
      anari::setParameter(device_, light_, "edge1", edge1);
      anari::setParameter(device_, light_, "edge2", edge2);
      anari::commitParameters(device_, light_);
    }
  }

  if (!registered_) {
    auto rp = static_cast<HdAnariRenderParam*>(renderParam);
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

void HdAnariLight::Finalize(HdRenderParam *renderParam) {
  auto rp = static_cast<HdAnariRenderParam*>(renderParam);
  rp->UnregisterLight(this);
  rp->MarkNewSceneVersion();

  anari::release(device_, light_);
  anari::release(device_, instance_);
}

PXR_NAMESPACE_CLOSE_SCOPE