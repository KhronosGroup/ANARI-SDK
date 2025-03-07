// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"

namespace helide {

struct TransformSampler : public Sampler
{
  TransformSampler(HelideGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;

  float4 getSample(const Geometry &g,
      const Ray &r,
      const UniformAttributeSet &instAttrV) const override;

 private:
  Attribute m_inAttribute{Attribute::NONE};
  mat4 m_transform{mat4(linalg::identity)};
};

} // namespace helide
