// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array1D.h"

namespace helide {

struct PrimitiveSampler : public Sampler
{
  PrimitiveSampler(HelideGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;

  float4 getSample(const Geometry &g,
      const Ray &r,
      const UniformAttributeSet &instAttrV) const override;

 private:
  helium::IntrusivePtr<Array1D> m_array;
  uint32_t m_offset{0};
};

} // namespace helide
