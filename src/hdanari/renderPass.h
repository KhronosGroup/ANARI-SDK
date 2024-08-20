// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/rect2i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/pxr.h>
#include <memory>

#include "renderParam.h"

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariRenderPass final : public HdRenderPass
{
  HdAnariRenderPass(HdRenderIndex *index,
      HdRprimCollection const &collection,
      std::shared_ptr<HdAnariRenderParam> renderParam);
  ~HdAnariRenderPass() override;

  void _Execute(HdRenderPassStateSharedPtr const &renderPassState,
      TfTokenVector const &renderTags) override;

 private:
  void _UpdateRenderer();
  void _UpdateFrame(
      const GfRect2i &size, const HdRenderPassAovBindingVector &aovBindings);
  void _UpdateCamera(const GfMatrix4d &invView, const GfMatrix4d &invProj);
  void _UpdateWorld();
  void _WriteAovs(const HdRenderPassAovBindingVector &aovBindings);

  std::shared_ptr<HdAnariRenderParam> _renderParam;

  HdRenderPassAovBindingVector _aovBindings;
  HdParsedAovTokenVector _aovNames;

  GfVec4f _clearColor;
  GfRect2i _frameSize;

  struct ViewInfo
  {
    GfMatrix4d view{1.f};
    GfMatrix4d proj{1.f};
    GfMatrix4d invView{1.f};
    GfMatrix4d invProj{1.f};
  } _camera;

  int _lastSettingsVersion{-1};
  int _lastSceneVersion{-1};

  struct AnariObjects
  {
    anari::Frame frame{nullptr};
    anari::Camera camera{nullptr};
    anari::Renderer renderer{nullptr};
    anari::World world{nullptr};
  } _anari;
};

PXR_NAMESPACE_CLOSE_SCOPE
