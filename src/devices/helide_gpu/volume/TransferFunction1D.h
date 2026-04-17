// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Volume.h"
#include "array/Array1D.h"
#include "gpu/GPUBuffer.h"
#include "gpu/GPUTexture.h"
#include "spatial_field/SpatialField.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <vector>

namespace helide_gpu {

struct TransferFunction1DGPUState
{
  GPUTexture tfTexture;
  SDL_GPUSampler *tfSampler{nullptr};
  GPUBuffer cubeVB;
};

struct TransferFunction1D : public Volume
{
  TransferFunction1D(HelideGPUDeviceGlobalState *s);
  ~TransferFunction1D() override;

  void commitParameters() override;
  void finalize() override;

  bool isValid() const override;
  box3 bounds() const override;

  void draw(SDL_GPURenderPass *pass,
      SDL_GPUCommandBuffer *cmd,
      const VolumeDrawContext &ctx) override;

  SDL_GPUTexture *fieldTexture() const override;
  SDL_GPUSampler *fieldSampler() const override;
  SDL_GPUTexture *tfTexture() const override;
  SDL_GPUSampler *tfSampler() const override;
  SDL_GPUBuffer *cubeVertexBuffer() const override;

  vec3 fieldOrigin() const override;
  vec3 fieldSpacing() const override;
  uvec3 fieldDims() const override;
  float stepSize() const override;
  float unitDistance() const override;
  vec2 valueRange() const override;

 private:
  void discretizeTF();
  void gpu_createResources();
  void gpu_freeResources();

  helium::ChangeObserverPtr<Array1D> m_color;
  helium::ChangeObserverPtr<Array1D> m_opacity;
  helium::IntrusivePtr<SpatialField> m_field;
  vec2 m_valueRange{0.f, 1.f};
  float m_unitDistance{1.f};
  vec4 m_uniformColor{1.f};
  float m_uniformOpacity{1.f};

  static constexpr int TF_DIM = 256;
  std::vector<vec4> m_tf;

  TransferFunction1DGPUState m_gpuState;
};

} // namespace helide_gpu
