// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SpatialField.h"
#include "array/Array3D.h"
#include "gpu/GPUTexture.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace helide_gpu {

struct StructuredRegularGPUState
{
  GPUTexture texture;
  SDL_GPUSampler *sampler{nullptr};
};

struct StructuredRegular : public SpatialField
{
  StructuredRegular(HelideGPUDeviceGlobalState *s);
  ~StructuredRegular() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;
  box3 bounds() const override;
  float stepSize() const override;
  vec3 origin() const override;
  vec3 spacing() const override;
  uvec3 dims() const override;
  SDL_GPUTexture *gpuTexture() const override;
  SDL_GPUSampler *gpuSampler() const override;

 private:
  void gpu_createTexture();
  void gpu_freeResources();

  helium::ChangeObserverPtr<Array3D> m_data;
  vec3 m_origin{0.f};
  vec3 m_spacing{1.f};
  StructuredRegularGPUState m_gpuState;
};

} // namespace helide_gpu
