// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PrimitiveSampler.h"
#include "scene/surface/geometry/Geometry.h"

namespace helide {

PrimitiveSampler::PrimitiveSampler(HelideGlobalState *s) : Sampler(s) {}

bool PrimitiveSampler::isValid() const
{
  return Sampler::isValid() && m_array;
}

void PrimitiveSampler::commit()
{
  Sampler::commit();
  m_array = getParamObject<Array1D>("array");
  m_offset =
      uint32_t(getParam<uint64_t>("offset", getParam<uint32_t>("offset", 0)));
}

float4 PrimitiveSampler::getSample(const Geometry &g, const Ray &r) const
{
  return m_array->readAsAttributeValue(uint32_t(r.primID + m_offset));
}

} // namespace helide
