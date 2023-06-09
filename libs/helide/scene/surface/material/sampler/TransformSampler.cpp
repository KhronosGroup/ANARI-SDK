// Copyright 2022 The Khronos Group
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
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
}

float4 TransformSampler::getSample(const Geometry &g, const Ray &r) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;
  auto av = linalg::mul(m_inTransform, g.getAttributeValue(m_inAttribute, r));
  return linalg::mul(m_outTransform, av);
}

} // namespace helide
