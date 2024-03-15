// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransformSampler.h"
#include "scene/surface/geometry/Geometry.h"

namespace helide {

TransformSampler::TransformSampler(HelideGlobalState *s) : Sampler(s) {}

bool TransformSampler::isValid() const
{
  return Sampler::isValid();
}

void TransformSampler::commit()
{
  Sampler::commit();
  m_inAttribute =
      attributeFromString(getParamString("inAttribute", "attribute0"));
  m_transform = getParam<mat4>("transform", mat4(linalg::identity));
}

float4 TransformSampler::getSample(const Geometry &g, const Ray &r) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;
  return linalg::mul(m_transform, g.getAttributeValue(m_inAttribute, r));
}

} // namespace helide
