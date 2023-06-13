// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array1D.h"

namespace helide {

struct Image1D : public Sampler
{
  Image1D(HelideGlobalState *d);

  bool isValid() const override;
  void commit() override;

  float4 getSample(const Geometry &g, const Ray &r) const override;

 private:
  helium::IntrusivePtr<Array1D> m_image;
  Attribute m_inAttribute{Attribute::NONE};
  WrapMode m_wrapMode{WrapMode::DEFAULT};
  bool m_linearFilter{true};
  mat4 m_inTransform{mat4(linalg::identity)};
  mat4 m_outTransform{mat4(linalg::identity)};
};

} // namespace helide
