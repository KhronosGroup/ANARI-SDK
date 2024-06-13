// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "hdAnariTypes.h"
// pxr
#include <pxr/imaging/hd/material.h>
// std
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMaterial final : public HdMaterial
{
  HdAnariMaterial(anari::Device d, const SdfPath &id);
  ~HdAnariMaterial() override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  anari::Material GetANARIMaterial() const;

 private:
  void _ProcessUsdPreviewSurfaceNode(HdMaterialNode node);
  void _ProcessTextureNode(
      HdMaterialNode node, const TfToken &inputName, const TfToken &outputName);

  struct AnariObjects
  {
    anari::Device device{nullptr};
    anari::Material material{nullptr};
    std::map<TfToken, anari::Sampler> textures;
  } _anari;
};

PXR_NAMESPACE_CLOSE_SCOPE