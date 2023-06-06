// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct Geometry;

struct Sampler : public Object
{
  Sampler(HelideGlobalState *d);

  virtual float4 getSample(const Geometry &g, const Ray &r) const = 0;

  static Sampler *createInstance(
      std::string_view subtype, HelideGlobalState *d);
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Sampler *, ANARI_SAMPLER);
