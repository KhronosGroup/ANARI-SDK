// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
#include "StructuredRegular.h"

namespace helide_gpu {

SpatialField::SpatialField(HelideGPUDeviceGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{}

SpatialField *SpatialField::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegular(s);
  return (SpatialField *)new UnknownObject(ANARI_SPATIAL_FIELD, s);
}

bool SpatialField::isValid() const
{
  return false;
}

box3 SpatialField::bounds() const
{
  return {};
}

float SpatialField::stepSize() const
{
  return 1.f;
}

vec3 SpatialField::origin() const
{
  return vec3(0.f);
}

vec3 SpatialField::spacing() const
{
  return vec3(1.f);
}

uvec3 SpatialField::dims() const
{
  return uvec3(0u);
}

SDL_GPUTexture *SpatialField::gpuTexture() const
{
  return nullptr;
}

SDL_GPUSampler *SpatialField::gpuSampler() const
{
  return nullptr;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::SpatialField *);
