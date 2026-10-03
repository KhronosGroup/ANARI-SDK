// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "TransformSampler.h"
#include "geometry/Geometry.h"

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
  // 'outTransform' is the spec name; 'transform' is helide's older name, still
  // read so existing scenes keep rendering.
  m_outTransform = getParam<mat4>(
      "outTransform", getParam<mat4>("transform", mat4(linalg::identity)));
  m_outOffset = getParam<float4>("outOffset", float4(0.f, 0.f, 0.f, 0.f));
}

float4 TransformSampler::getSample(
    const Geometry &g, const Ray &r, const UniformAttributeSet &instAttrV) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  const auto &ia = getUniformAttribute(instAttrV, m_inAttribute);
  return linalg::mul(
             m_outTransform, ia ? *ia : g.getAttributeValue(m_inAttribute, r))
      + m_outOffset;
}

} // namespace helide
