// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImageSampler.h"
#include "array/Array1D.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace helide_gpu {

struct Image1D : public ImageSampler
{
  Image1D(HelideGPUDeviceGlobalState *s);
  ~Image1D() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void gpu_createTexture();

  helium::ChangeObserverPtr<Array1D> m_image;
};

} // namespace helide_gpu
