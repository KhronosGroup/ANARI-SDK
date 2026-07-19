// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImageSampler.h"
#include "array/Array2D.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace helide_gpu {

struct Image2D : public ImageSampler
{
  Image2D(HelideGPUDeviceGlobalState *s);
  ~Image2D() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void gpu_createTexture();

  helium::ChangeObserverPtr<Array2D> m_image;
};

} // namespace helide_gpu
