// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../material.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/pxr.h>

PXR_NAMESPACE_OPEN_SCOPE

struct HdAnariMdlMaterial final
{
  static anari::Material CreateMaterial(anari::Device device,
      const HdMaterialNetwork2Interface &materialNetworkIface);

  static HdAnariMaterial::PrimvarMapping EnumeratePrimvars(
      const HdMaterialNetwork2Interface &materialNetworkInterface,
      TfToken terminal);

  static void SyncMaterialParameters(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping,
      const HdAnariMaterial::SamplerMapping &samplerMapping);

  static HdAnariMaterial::TextureDescMapping EnumerateTextures(
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken terminalName);

  static void ProcessUsdPreviewSurfaceNode(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken terminal,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping,
      const HdAnariMaterial::SamplerMapping &samplerMapping);

  static void ProcessMdlNode(anari::Device device,
      anari::Material material,
      const HdMaterialNetwork2Interface &materialNetworkIface,
      TfToken terminal,
      const HdAnariMaterial::PrimvarBinding &primvarBinding,
      const HdAnariMaterial::PrimvarMapping &primvarMapping,
      const HdAnariMaterial::SamplerMapping &samplerMapping);
};

PXR_NAMESPACE_CLOSE_SCOPE
