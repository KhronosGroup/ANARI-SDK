// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "hdAnariTypes.h"
// pxr
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hd/material.h>
// std
#include <map>

PXR_NAMESPACE_OPEN_SCOPE


struct HdAnariMaterial : public HdMaterial {
  HdAnariMaterial(anari::Device d, const SdfPath &id);
  ~HdAnariMaterial() override;

  virtual anari::Material GetANARIMaterial() const = 0;

};

struct HdAnariMatteMaterial final : public HdAnariMaterial
{
  HdAnariMatteMaterial(anari::Device d, const SdfPath &id);
  ~HdAnariMatteMaterial() override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  anari::Material GetANARIMaterial() const override;

 private:
  void _ProcessUsdPreviewSurfaceNode(const HdMaterialNetwork2Interface& materialNetwork, TfToken terminal);

  struct AnariObjects
  {
    anari::Device device{nullptr};
    anari::Material material{nullptr};
  } _anari;
};

struct HdAnariPhysicallyBasedMaterial final : public HdAnariMaterial
{
  HdAnariPhysicallyBasedMaterial(anari::Device d, const SdfPath &id);
  ~HdAnariPhysicallyBasedMaterial() override;

  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

  HdDirtyBits GetInitialDirtyBitsMask() const override;

  anari::Material GetANARIMaterial() const override;

 private:
  void _ProcessUsdPreviewSurfaceNode(const HdMaterialNetwork2Interface& materialNetwork, TfToken terminal);

  struct AnariObjects
  {
    anari::Device device{nullptr};
    anari::Material material{nullptr};
    std::map<TfToken, anari::Sampler> textures;
  } _anari;
};


PXR_NAMESPACE_CLOSE_SCOPE