// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array2D.h"

namespace helide {

struct Image2D : public Sampler
{
  Image2D(HelideGlobalState *d);

  bool isValid() const override;
  void commitParameters() override;

  float4 getSample(const Geometry &g,
      const Ray &r,
      const UniformAttributeSet &instAttrV) const override;

 private:
  helium::IntrusivePtr<Array2D> m_image;
  Attribute m_inAttribute{Attribute::NONE};
  WrapMode m_wrapMode1{WrapMode::DEFAULT};
  WrapMode m_wrapMode2{WrapMode::DEFAULT};
  bool m_linearFilter{true};
  mat4 m_inTransform{mat4(linalg::identity)};
  float4 m_inOffset{0.f, 0.f, 0.f, 0.f};
  mat4 m_outTransform{mat4(linalg::identity)};
  float4 m_outOffset{0.f, 0.f, 0.f, 0.f};
};

} // namespace helide
