// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/base/tf/staticTokens.h>

#define HDANARI_MATERIAL_TOKENS \
  (a) \
  (b) \
  (bias) \
  (clearcoat)  \
  (clearcoatRoughness) \
  (diffuseColor) \
  (emissiveColor) \
  (fallback) \
  (file) \
  (filename) \
  (g) \
  (infoFilename) \
  (ior) \
  (metallic) \
  (normal) \
  (occlusion) \
  (opacity) \
  (opacityThreshold) \
  (r) \
  (raw) \
  (rgb) \
  (roughness) \
  (scale) \
  (sourceColorSpace) \
  (specularColor) \
  (sRGB) \
  (st) \
  (surface) \
  (UsdPreviewSurface) \
  (UsdUVTexture) \
  (useSpecularWorkflow) \
  (varname) \
  (wrapS) \
  (wrapT)

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_PUBLIC_TOKENS(HdAnariMaterialTokens, HDANARI_MATERIAL_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE
