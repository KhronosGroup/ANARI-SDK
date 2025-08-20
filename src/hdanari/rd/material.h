// Copyright 2024-2025 The Khronos Group
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
  enum class MaterialType
  {
    Matte,
    PhysicallyBased,
    Mdl,
  };

  using PrimvarBinding =
      std::map<TfToken, TfToken>; // Maps a primvar to an anari attribute name
  using PrimvarMapping =
      std::map<SdfPath, TfToken>; // Maps an input to a primvar name
  using TextureDescMapping = std::map<SdfPath,
      HdAnariTextureLoader::TextureDesc>; // Maps a path to a usduvtexture and
                                          // related info

  using SamplerMapping = std::map<SdfPath,
      anari::Sampler>; // Maps a usduvtexture path to the matching sampler

  HdAnariMaterial(anari::Device d, const SdfPath &id);
  ~HdAnariMaterial() override;

  // Get a material instance.
  anari::Material GetAnariMaterial() const;

  // Create thet transient materialnetwork2 representation. To be used by the
  // above ProcessMaterialNetwork call.
  void Sync(HdSceneDelegate *sceneDelegate,
      HdRenderParam *renderParam,
      HdDirtyBits *dirtyBits) override;

  // Finalize
  void Finalize(HdRenderParam *renderParam) override;

  // Get initial dirty bits mask
  HdDirtyBits GetInitialDirtyBitsMask() const override;

  const PrimvarBinding &GetPrimvarBinding() const
  {
    return attributes_;
  }

 protected:
  TextureDescMapping textures_{};
  SamplerMapping samplers_{};
  PrimvarMapping primvars_{};
  PrimvarBinding attributes_{};

  HdMaterialNetwork2 materialNetwork2_{};

  anari::Device device_{};
  anari::Material material_{};

  MaterialType materialType_{MaterialType::Matte};

 private:
  // Convert the given material network to the newer HdMaterialNetwork2
  static HdMaterialNetwork2 convertToHdMaterialNetwork2(
      const HdMaterialNetworkMap &hdNetworkMap);

  // Return a primvar to anari parameter mapping that can be used to correctly
  // expose primvars on the meshes.
  static PrimvarBinding BuildPrimvarBinding(const PrimvarMapping &primvarNames);

  static SamplerMapping CreateSamplers(
      anari::Device device, const TextureDescMapping &textureDescs);
  static void ReleaseSamplers(anari::Device, const SamplerMapping &samplers);
};

PXR_NAMESPACE_CLOSE_SCOPE