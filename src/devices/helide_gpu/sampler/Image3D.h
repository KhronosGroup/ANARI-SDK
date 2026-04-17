// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImageSampler.h"
#include "array/Array3D.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace helide_gpu {

struct Image3D : public ImageSampler
{
  Image3D(HelideGPUDeviceGlobalState *s);
  ~Image3D() override = default;

  void commitParameters() override;
  void finalize() override;

 private:
  void gpu_createTexture();

  helium::ChangeObserverPtr<Array3D> m_image;
};

} // namespace helide_gpu
