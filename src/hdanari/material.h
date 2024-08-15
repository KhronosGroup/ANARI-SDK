// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "material/textureLoader.h"

// pxr
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/enums.h>
#include <pxr/imaging/hd/instancer.h>
#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/materialNetwork2Interface.h>
#include <pxr/imaging/hd/mesh.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/usd/sdf/path.h>

// std
#include <anari/anari_cpp.hpp>
#include <map>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMaterial : public HdMaterial
{
 public:
  using PrimvarBinding = std::map<TfToken, TfToken>; // Maps a primvar to an anari attribute name
  using PrimvarMapping = std::map<SdfPath, TfToken>; // Maps an input to a primvar name
  using TextureDescMapping = std::map<SdfPath, HdAnariTextureLoader::TextureDesc>; // Maps a path to a usduvtexture and related info

  using SamplerMapping = std::map<SdfPath, anari::Sampler>;  // Maps a usduvtexture path to the matching sampler

  HdAnariMaterial(anari::Device d, const SdfPath &id);
  ~HdAnariMaterial() override;

  // Get a material instance.
  anari::Material GetAnariMaterial() const;

  // Create thet transient materialnetwork2 representation. To be used by the above ProcessMaterialNetwork call.
  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

  // Finalize
  void Finalize(HdRenderParam* renderParam) override;

  // Get initial dirty bits mask
  HdDirtyBits GetInitialDirtyBitsMask() const override;

  const PrimvarBinding& GetPrimvarBinding() const { return attributes_; }

protected:
  // Load and returns all textures associated with this material.
  virtual TextureDescMapping  EnumerateTextures(const HdMaterialNetwork2Interface &materialNetworkInterface, TfToken terminal) const = 0;
  virtual PrimvarMapping EnumeratePrimvars(const HdMaterialNetwork2Interface &materialNetworkInterface, TfToken terminal) const = 0;

  virtual anari::Material GetOrCreateMaterial(
    const HdMaterialNetwork2Interface& materialNetworkIface,
    const PrimvarBinding& primvarBinding,
    const PrimvarMapping& primvarMapping,
    const SamplerMapping& samplerMapping
  ) const = 0;

  TextureDescMapping textures_;
  SamplerMapping samplers_;
  PrimvarMapping primvars_;
  PrimvarBinding attributes_;

  HdMaterialNetwork2 materialNetwork2_;
  anari::Device device_;
  anari::Material material_;

 private:
  // Convert the given material network to the newer HdMaterialNetwork2
  static HdMaterialNetwork2 convertToHdMaterialNetwork2(const HdMaterialNetworkMap &hdNetworkMap);

  // Return a primvar to anari parameter mapping that can be used to correctly expose primvars on the meshes.
  static PrimvarBinding BuildPrimvarBinding(const PrimvarMapping& primvarNames);

  static SamplerMapping CreateSamplers(anari::Device device, const TextureDescMapping& textureDescs);
  static void ReleaseSamplers(anari::Device, const SamplerMapping& samplers);
};


PXR_NAMESPACE_CLOSE_SCOPE