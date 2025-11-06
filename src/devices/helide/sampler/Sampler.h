// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct Geometry;

struct Sampler : public Object
{
  Sampler(HelideGlobalState *d);
  virtual ~Sampler() override = default;

  virtual float4 getSample(const Geometry &g,
      const Ray &r,
      const UniformAttributeSet &instAttrV) const = 0;

  static Sampler *createInstance(
      std::string_view subtype, HelideGlobalState *d);
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Sampler *, ANARI_SAMPLER);
