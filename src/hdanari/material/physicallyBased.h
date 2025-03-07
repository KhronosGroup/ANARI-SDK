// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"

#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariPhysicallyBasedMaterial final : public HdAnariMaterial
{
public:
  using HdAnariMaterial::HdAnariMaterial;

protected:
  TextureDescMapping EnumerateTextures(const HdMaterialNetwork2Interface &materialNetworkInterface, TfToken terminal) const override;
  PrimvarMapping EnumeratePrimvars(const HdMaterialNetwork2Interface &materialNetworkInterface, TfToken terminal) const override;

  anari::Material GetOrCreateMaterial(
    const HdMaterialNetwork2Interface& materialNetworkIface,
    const PrimvarBinding& primvarBinding,
    const PrimvarMapping& primvarMapping,
    const SamplerMapping& samplerMapping
  ) const override;

 private:
  void ProcessUsdPreviewSurfaceNode(anari::Material material, const HdMaterialNetwork2Interface &materialNetworkIface, TfToken terminal,
      const PrimvarBinding& primvarBinding, const PrimvarMapping& primvarMapping, const SamplerMapping& samplerMapping) const;
};

PXR_NAMESPACE_CLOSE_SCOPE
