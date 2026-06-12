// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "renderPass.h"

#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/debug.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/camera.h>
#include <pxr/imaging/hd/debugCodes.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <anari/anari_cpp.hpp>
#include <anari/anari_cpp/anari_cpp_impl.hpp>
// pxr
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <stddef.h>
#include <stdint.h>
#include <cmath>
#include <vector>

#include "geometry.h"
#include "mesh.h"
#include "renderBuffer.h"
#include "renderDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

// Helper functions ///////////////////////////////////////////////////////////

static GfVec4f ComputeClearColor(VtValue const &clearValue)
{
  HdTupleType type = HdGetValueTupleType(clearValue);
  if (type.count != 1) {
    return GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);
  }

  switch (type.type) {
  case HdTypeFloatVec3: {
    GfVec3f f = *(static_cast<const GfVec3f *>(HdGetValueData(clearValue)));
    return GfVec4f(f[0], f[1], f[2], 1.0f);
  }
  case HdTypeFloatVec4: {
    GfVec4f f = *(static_cast<const GfVec4f *>(HdGetValueData(clearValue)));
    return f;
  }
  case HdTypeDoubleVec3: {
    GfVec3d f = *(static_cast<const GfVec3d *>(HdGetValueData(clearValue)));
    return GfVec4f(f[0], f[1], f[2], 1.0f);
  }
  case HdTypeDoubleVec4: {
    GfVec4d f = *(static_cast<const GfVec4d *>(HdGetValueData(clearValue)));
    return GfVec4f(f);
  }
  default:
    return GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);
  }
}

static GfRect2i _GetDataWindow(
    HdRenderPassStateSharedPtr const &renderPassState)
{
  const CameraUtilFraming &framing = renderPassState->GetFraming();
  if (framing.IsValid())
    return framing.dataWindow;
  else {
    const GfVec4f vp = renderPassState->GetViewport();
    return GfRect2i(GfVec2i(0), int(vp[2]), int(vp[3]));
  }
}

// HdAnariRenderPass definitions //////////////////////////////////////////////

HdAnariRenderPass::HdAnariRenderPass(HdRenderIndex *index,
    HdRprimCollection const &collection,
    std::shared_ptr<HdAnariRenderParam> renderParam)
    : HdRenderPass(index, collection), _renderParam(renderParam)
{
  if (!_renderParam)
    return;

  auto d = _renderParam->GetANARIDevice();

  _anari.frame = anari::newObject<anari::Frame>(d);
  anari::setParameter(d, _anari.frame, "channel.color", ANARI_FLOAT32_VEC4);
  anari::setParameter(d, _anari.frame, "channel.depth", ANARI_FLOAT32);
  anari::setParameter(d, _anari.frame, "channel.primitiveId", ANARI_UINT32);
  anari::setParameter(d, _anari.frame, "channel.objectId", ANARI_UINT32);
  anari::setParameter(d, _anari.frame, "channel.instanceId", ANARI_UINT32);

  _anari.camera = anari::newObject<anari::Camera>(d, "perspective");
  _anari.world = anari::newObject<anari::World>(d);

  _UpdateRenderer();

  anari::setParameter(d, _anari.frame, "camera", _anari.camera);
  anari::setParameter(d, _anari.frame, "renderer", _anari.renderer);
  anari::setParameter(d, _anari.frame, "world", _anari.world);

  anari::commitParameters(d, _anari.frame);
}

HdAnariRenderPass::~HdAnariRenderPass()
{
  if (!_renderParam)
    return;

  auto d = _renderParam->GetANARIDevice();
  anari::discard(d, _anari.frame);
  anari::wait(d, _anari.frame);
  anari::release(d, _anari.frame);
  anari::release(d, _anari.camera);
  anari::release(d, _anari.renderer);
  anari::release(d, _anari.world);
}

void HdAnariRenderPass::_Execute(
    HdRenderPassStateSharedPtr const &renderPassState,
    TfTokenVector const &renderTags)
{
  if (_renderParam) {
    // UsdGeomCamera exposure travels with the scene; fold its linear scale in
    // alongside the 'exposure' render setting. Defaults to 1 (neutral).
    if (const HdCamera *camera = renderPassState->GetCamera())
      _cameraExposureScale = camera->GetLinearExposureScale();

    bool sceneChanged = false;
    sceneChanged |= _UpdateFrame(
        _GetDataWindow(renderPassState), renderPassState->GetAovBindings());
    sceneChanged |= _UpdateRenderer();
    sceneChanged |= _UpdateCamera(renderPassState->GetWorldToViewMatrix(),
        renderPassState->GetProjectionMatrix());
    sceneChanged |= _UpdateWorld();
    _WriteAovs(renderPassState->GetAovBindings(), sceneChanged);
  }
}

bool HdAnariRenderPass::_UpdateRenderer()
{
  HdRenderDelegate *renderDelegate = GetRenderIndex()->GetRenderDelegate();
  int currentSettingsVersion = renderDelegate->GetRenderSettingsVersion();
  if (_lastSettingsVersion == currentSettingsVersion)
    return false;

  {
    auto d = _renderParam->GetANARIDevice();

    if (const auto renderSubtype = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->renderSubtype);
        TF_VERIFY(renderSubtype.IsHolding<std::string>())) {
      if (_anari.renderer) {
        anari::release(d, _anari.renderer);
        anari::commitParameters(d, d);
      }
      _anari.renderer = anari::newObject<anari::Renderer>(
          d, renderSubtype.UncheckedGet<std::string>().c_str());
      anari::setParameter(d, _anari.frame, "renderer", _anari.renderer);
      anari::commitParameters(d, _anari.frame);
    }

    if (const VtValue ar = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->ambientRadiance);
        TF_VERIFY(ar.CanCast<float>())) {
      auto v = VtValue::Cast<float>(ar).UncheckedGet<float>();
      anari::setParameter(d,
          _anari.renderer,
          "ambientRadiance",
          VtValue::Cast<float>(ar).UncheckedGet<float>());
    }

    if (const auto ac = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->ambientColor);
        TF_VERIFY(ac.IsHolding<GfVec3f>())) {
      anari::setParameter(
          d, _anari.renderer, "ambientColor", ac.UncheckedGet<GfVec3f>());
    }

    if (const auto as = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->ambientSamples);
        TF_VERIFY(as.IsHolding<int>())) {
      anari::setParameter(
          d, _anari.renderer, "ambientSamples", as.UncheckedGet<int>());
    }

    if (const auto sp = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->sampleLimit);
        TF_VERIFY(sp.IsHolding<int>())) {
      anari::setParameter(
          d, _anari.renderer, "sampleLimit", sp.UncheckedGet<int>());
    }

    if (const auto rb = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->maxRayDepth);
        TF_VERIFY(rb.IsHolding<int>())) {
      anari::setParameter(
          d, _anari.renderer, "maxRayDepth", rb.UncheckedGet<int>());
    }

    if (const auto ps = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->pixelSamples);
        TF_VERIFY(ps.IsHolding<int>())) {
      anari::setParameter(
          d, _anari.renderer, "pixelSamples", ps.UncheckedGet<int>());
    }

    if (const auto denoise =
        renderDelegate->GetRenderSetting(HdAnariRenderSettingsTokens->denoise);
        TF_VERIFY(denoise.IsHolding<bool>())) {
      anari::setParameter(
          d, _anari.renderer, "denoise", denoise.UncheckedGet<bool>());
    }

    if (const auto debugMethod = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->debugMethod);
        debugMethod.IsHolding<std::string>()) {
      anari::setParameter(d,
          _anari.renderer,
          "method",
          debugMethod.UncheckedGet<std::string>().c_str());
    }

    // Exposure and tonemapping are applied by hdanari to the color AOV, not
    // by the ANARI renderer, so these are read but not forwarded to the device.
    if (const auto exposure = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->exposure);
        exposure.CanCast<float>()) {
      _exposure = VtValue::Cast<float>(exposure).UncheckedGet<float>();
    }

    if (const auto tonemap = renderDelegate->GetRenderSetting(
        HdAnariRenderSettingsTokens->tonemap);
        tonemap.IsHolding<bool>()) {
      _tonemap = tonemap.UncheckedGet<bool>();
    }

    anari::commitParameters(d, _anari.renderer);

    _lastSettingsVersion = currentSettingsVersion;
  }

  return true;
}

bool HdAnariRenderPass::_UpdateFrame(
    const GfRect2i &size, const HdRenderPassAovBindingVector &aovBindings)
{
  auto d = _renderParam->GetANARIDevice();
  bool changed = false;

  if (_frameSize != size) {
    _frameSize = size;
    const uint32_t s[2] = {
        (uint32_t)size.GetWidth(), (uint32_t)size.GetHeight()};
    anari::setParameter(d, _anari.frame, "size", s);
    anari::commitParameters(d, _anari.frame);
    changed = true;
  }

  bool aovDirty = (_aovBindings.empty() || _aovBindings != aovBindings);
  if (aovDirty) {
    _aovBindings = aovBindings;
    _aovNames.resize(_aovBindings.size());
    for (size_t i = 0; i < _aovBindings.size(); ++i)
      _aovNames[i] = HdParsedAovToken(_aovBindings[i].aovName);
  }

  for (int aovIndex = 0; aovIndex < _aovBindings.size(); aovIndex++) {
    if (_aovNames[aovIndex].name == HdAovTokens->color) {
      GfVec4f clearColor = ComputeClearColor(_aovBindings[aovIndex].clearValue);
      if (clearColor != _clearColor) {
        _clearColor = clearColor;
        anari::setParameter(d, _anari.renderer, "background", _clearColor);
        anari::commitParameters(d, _anari.renderer);
        changed = true;
      }
      break;
    }
  }

  return changed;
}

bool HdAnariRenderPass::_UpdateCamera(
    const GfMatrix4d &view, const GfMatrix4d &proj)
{
  auto d = _renderParam->GetANARIDevice();

  if (_camera.view == view && _camera.proj == proj)
    return false;

  {
    _camera.view = view;
    _camera.proj = proj;
    _camera.invView = view.GetInverse();
    _camera.invProj = proj.GetInverse();

    const GfVec3f projDir =
        GfVec3f(_camera.invProj.Transform(GfVec3f(0, 0, -1)));
    const GfVec3f origin = GfVec3f(_camera.invView.Transform(GfVec3f(0, 0, 0)));
    const GfVec3f dir =
        GfVec3f(_camera.invView.TransformDir(projDir).GetNormalized());
    const GfVec3f up =
        GfVec3f(_camera.invView.TransformDir(GfVec3f(0, 1, 0)).GetNormalized());

    anari::setParameter(d, _anari.camera, "position", origin);
    anari::setParameter(d, _anari.camera, "direction", dir);
    anari::setParameter(d, _anari.camera, "up", up);

    const float aspect = _frameSize.GetWidth() / float(_frameSize.GetHeight());
    anari::setParameter(d, _anari.camera, "aspect", aspect);

    double prjMatrix[4][4];
    _camera.proj.Get(prjMatrix);
    const float fov = 2.0 * std::atan(1.0 / prjMatrix[1][1]);
    anari::setParameter(d, _anari.camera, "fovy", fov);

    anari::commitParameters(d, _anari.camera);
  }

  return true;
}

bool HdAnariRenderPass::_UpdateWorld()
{
  auto sceneVersion = _renderParam->SceneVersion();
  if (sceneVersion <= _lastSceneVersion)
    return false;

  std::vector<anari::Instance> instances;

  for (const auto *light : _renderParam->Lights())
    instances.push_back(light->GetAnariLightInstance());

  for (const auto *geometry : _renderParam->Geometries())
    geometry->GatherInstances(instances);

  auto d = _renderParam->GetANARIDevice();
  if (instances.empty())
    anari::unsetParameter(d, _anari.world, "instance");
  else {
    anari::setParameterArray1D(
        d, _anari.world, "instance", instances.data(), instances.size());
  }
  anari::commitParameters(d, _anari.world);

  _lastSceneVersion = sceneVersion;

  return true;
}

bool HdAnariRenderPass::_UpdateProgress()
{
  auto d = _renderParam->GetANARIDevice();

  int numSamples = 0;
  const bool hasNumSamples = anari::getProperty(
      d, _anari.frame, "numSamples", numSamples, ANARI_WAIT);

  HdRenderDelegate *renderDelegate = GetRenderIndex()->GetRenderDelegate();
  const VtValue sp = renderDelegate->GetRenderSetting(
      HdAnariRenderSettingsTokens->sampleLimit);
  const int sampleLimit = sp.IsHolding<int>() ? sp.UncheckedGet<int>() : 0;

  // Devices that don't report sample progress render a complete frame in a
  // single pass, so a finished frame is already fully converged.
  const int completedSamples = hasNumSamples ? numSamples : sampleLimit;
  _renderParam->SetProgress(completedSamples, sampleLimit);

  if (!hasNumSamples)
    return true;
  return sampleLimit > 0 && numSamples >= sampleLimit;
}

void HdAnariRenderPass::_WriteAovs(
    const HdRenderPassAovBindingVector &aovBindings, bool sceneChanged)
{
  auto d = _renderParam->GetANARIDevice();
  if (!anari::isReady(d, _anari.frame))
    return;

  const float exposureScale = std::exp2(_exposure) * _cameraExposureScale;
  for (auto &aov : aovBindings) {
    auto *b = (HdAnariRenderBuffer *)aov.renderBuffer;
    b->CopyFromAnariFrame(
        d, _anari.frame, aov.aovName, aov.clearValue, exposureScale, _tonemap);
  }

  // A scene change this frame restarted accumulation, so report progress as
  // unconverged regardless of the (now stale) sample count.
  const bool converged = !sceneChanged && _UpdateProgress();
  for (auto &aov : aovBindings) {
    static_cast<HdAnariRenderBuffer *>(aov.renderBuffer)
        ->SetConverged(converged);
  }

  // Kick the next accumulation pass only while there is more work to do;
  // re-rendering a converged frame would never let usdrecord terminate.
  if (!converged)
    anari::render(d, _anari.frame);
}

PXR_NAMESPACE_CLOSE_SCOPE