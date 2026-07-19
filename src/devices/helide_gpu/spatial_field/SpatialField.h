// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
// SDL3
#include <SDL3/SDL_gpu.h>

namespace helide_gpu {

struct SpatialField : public Object
{
  SpatialField(HelideGPUDeviceGlobalState *d);
  virtual ~SpatialField() = default;
  static SpatialField *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *d);

  virtual bool isValid() const override;
  virtual box3 bounds() const;
  virtual float stepSize() const;
  virtual vec3 origin() const;
  virtual vec3 spacing() const;
  virtual uvec3 dims() const;
  virtual SDL_GPUTexture *gpuTexture() const;
  virtual SDL_GPUSampler *gpuSampler() const;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(
    helide_gpu::SpatialField *, ANARI_SPATIAL_FIELD);
