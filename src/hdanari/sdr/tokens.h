// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/tf/staticTokens.h>
#include <pxr/pxr.h>

// clang-format off
#define HDANARI_SDR_TOKENS \
  ((bool1, "bool")) \
  ((float1, "float")) \
  ((float2, "float2")) \
  ((float2x2, "float2x2")) \
  ((float2x3, "float2x3")) \
  ((float2x4, "float2x4")) \
  ((float3, "float3")) \
  ((float3x2, "float3x2")) \
  ((float3x3, "float3x3")) \
  ((float3x4, "float3x4")) \
  ((float4, "float4")) \
  ((float4x2, "float4x2")) \
  ((float4x3, "float4x3")) \
  ((float4x4, "float4x4")) \
  ((int1, "int")) \
  ((int2, "int2")) \
  ((int3, "int3")) \
  ((int4, "int4")) \
  ((string, "string")) \
  ((texture2d, "texture_2d"))  \
  ((texture3d, "texture_3d"))  \
  ((textureCube, "texture_cube"))  \
  ((texturePtex, "texture_ptex"))  \
  (mdl)
// clang-format on

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_PUBLIC_TOKENS(HdAnariSdrTokens, HDANARI_SDR_TOKENS);

PXR_NAMESPACE_CLOSE_SCOPE
