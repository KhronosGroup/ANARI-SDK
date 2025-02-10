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

void TransformSampler::commitParameters()
{
  Sampler::commitParameters();
  m_inAttribute =
      attributeFromString(getParamString("inAttribute", "attribute0"));
  m_transform = getParam<mat4>("transform", mat4(linalg::identity));
}

float4 TransformSampler::getSample(
    const Geometry &g, const Ray &r, const UniformAttributeSet &instAttrV) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  const auto &ia = getUniformAttribute(instAttrV, m_inAttribute);
  return linalg::mul(
      m_transform, ia ? *ia : g.getAttributeValue(m_inAttribute, r));
}

} // namespace helide
