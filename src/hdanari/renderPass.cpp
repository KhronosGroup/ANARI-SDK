// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "renderPass.h"

#include <anari/frontend/anari_enums.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/staticData.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/renderDelegate.h>
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

  _anari.renderer = anari::newObject<anari::Renderer>(d, "default");
  _anari.camera = anari::newObject<anari::Camera>(d, "perspective");
  _anari.world = anari::newObject<anari::World>(d);

  anari::setParameter(d, _anari.frame, "camera", _anari.camera);
  anari::setParameter(d, _anari.frame, "renderer", _anari.renderer);
  anari::setParameter(d, _anari.frame, "world", _anari.world);

  anari::setParameter(d, _anari.renderer, "ambientRadiance", 1.f);

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
    _UpdateFrame(
        _GetDataWindow(renderPassState), renderPassState->GetAovBindings());
    _UpdateRenderer();
    _UpdateCamera(renderPassState->GetWorldToViewMatrix(),
        renderPassState->GetProjectionMatrix());
    _UpdateWorld();
    _WriteAovs(renderPassState->GetAovBindings());
  }
}

void HdAnariRenderPass::_UpdateRenderer()
{
  HdRenderDelegate *renderDelegate = GetRenderIndex()->GetRenderDelegate();
  int currentSettingsVersion = renderDelegate->GetRenderSettingsVersion();
  if (_lastSettingsVersion != currentSettingsVersion) {
    auto d = _renderParam->GetANARIDevice();

    const float ar = renderDelegate->GetRenderSetting<float>(
        HdAnariRenderSettingsTokens->ambientRadiance, 1.f);
    anari::setParameter(d, _anari.renderer, "ambientRadiance", ar);

    anari::commitParameters(d, _anari.renderer);

    _lastSettingsVersion = currentSettingsVersion;
  }
}

void HdAnariRenderPass::_UpdateFrame(
    const GfRect2i &size, const HdRenderPassAovBindingVector &aovBindings)
{
  auto d = _renderParam->GetANARIDevice();

  if (_frameSize != size) {
    _frameSize = size;
    const uint32_t s[2] = {
        (uint32_t)size.GetWidth(), (uint32_t)size.GetHeight()};
    anari::setParameter(d, _anari.frame, "size", s);
    anari::commitParameters(d, _anari.frame);
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
      }
      break;
    }
  }
}

void HdAnariRenderPass::_UpdateCamera(
    const GfMatrix4d &view, const GfMatrix4d &proj)
{
  auto d = _renderParam->GetANARIDevice();

  if (_camera.view != view || _camera.proj != proj) {
    _camera.view = view;
    _camera.proj = proj;
    _camera.invView = view.GetInverse();
    _camera.invProj = proj.GetInverse();

    const GfVec3f projDir = _camera.invProj.Transform(GfVec3f(0, 0, -1));
    const GfVec3f origin = _camera.invView.Transform(GfVec3f(0, 0, 0));
    const GfVec3f dir = _camera.invView.TransformDir(projDir).GetNormalized();
    const GfVec3f up =
        _camera.invView.TransformDir(GfVec3f(0, 1, 0)).GetNormalized();

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
}

void HdAnariRenderPass::_UpdateWorld()
{
  auto sceneVersion = _renderParam->SceneVersion();
  if (sceneVersion <= _lastSceneVersion)
    return;

  std::vector<anari::Instance> instances;

  for (const auto *mesh : _renderParam->Meshes())
    mesh->AddInstances(instances);

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
}

void HdAnariRenderPass::_WriteAovs(
    const HdRenderPassAovBindingVector &aovBindings)
{
  auto d = _renderParam->GetANARIDevice();
  if (!anari::isReady(d, _anari.frame))
    return;

  for (auto &aov : aovBindings) {
    auto *b = (HdAnariRenderBuffer *)aov.renderBuffer;
    b->CopyFromAnariFrame(d, _anari.frame, aov.aovName, aov.clearValue);
  }

  anari::render(d, _anari.frame);
}

PXR_NAMESPACE_CLOSE_SCOPE