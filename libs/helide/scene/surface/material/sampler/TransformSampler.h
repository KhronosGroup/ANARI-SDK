// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"

namespace helide {

struct TransformSampler : public Sampler
{
  TransformSampler(HelideGlobalState *d);

  bool isValid() const override;
  void commit() override;

  float4 getSample(const Geometry &g, const Ray &r) const override;

 private:
  Attribute m_inAttribute{Attribute::NONE};
  mat4 m_transform{mat4(linalg::identity)};
};

} // namespace helide
