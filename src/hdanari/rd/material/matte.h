// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"

#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMatteMaterial final
{
  static HdAnariMaterial::TextureDescMapping EnumerateTextures(
      const HdMaterialNetwork2Interface &materialNetworkInterface,
      TfToken terminal);
  static HdAnariMaterial::PrimvarMapping EnumeratePrimvars(
      const HdMaterialNetwork2Interface &materialNetworkInterface,
      TfToken terminal);

  static anari::Material CreateMaterial(anari::Device device,
      const HdMaterialNetwork2Interface &materialNetworkIface);

  static void SyncMaterialParameters(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping,
      const HdAnariMaterial::SamplerMapping &samplerMapping);

  static void ProcessUsdPreviewSurfaceNode(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken terminal,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping,
      const HdAnariMaterial::SamplerMapping &samplerMapping);
};

PXR_NAMESPACE_CLOSE_SCOPE
