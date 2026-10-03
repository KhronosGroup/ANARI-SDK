// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array1D.h"
// std
#include <string>

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
  uint64_t readOffset(const std::string &name) const;

  helium::IntrusivePtr<Array1D> m_array;
  uint32_t m_offset{0};
};

} // namespace helide
