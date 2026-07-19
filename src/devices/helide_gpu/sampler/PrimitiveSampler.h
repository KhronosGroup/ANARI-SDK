// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImageSampler.h" // for ImageSamplerGPUState
#include "Sampler.h"
#include "array/Array1D.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace helide_gpu {

struct PrimitiveSampler : public Sampler
{
  PrimitiveSampler(HelideGPUDeviceGlobalState *s);
  ~PrimitiveSampler() override;

  void commitParameters() override;
  void finalize() override;

  int samplerMode() const override;
  bool isValid() const override;

  const ImageSamplerGPUState &gpuState() const;
  uint64_t primitiveOffset() const;
  uint32_t arraySize() const;

 private:
  void gpu_createTexture();
  void gpu_freeResources();

  helium::ChangeObserverPtr<Array1D> m_array;
  uint64_t m_offset{0};
  uint32_t m_arraySize{0};
  ImageSamplerGPUState m_gpuState;
};

} // namespace helide_gpu
