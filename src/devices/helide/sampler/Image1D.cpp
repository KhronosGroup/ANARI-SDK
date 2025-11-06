// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image1D.h"
#include "geometry/Geometry.h"

namespace helide {

Image1D::Image1D(HelideGlobalState *s) : Sampler(s) {}

bool Image1D::isValid() const
{
  return Sampler::isValid() && m_image;
}

void Image1D::commitParameters()
{
  Sampler::commitParameters();
  m_image = getParamObject<Array1D>("image");
  m_inAttribute =
      attributeFromString(getParamString("inAttribute", "attribute0"));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode = wrapModeFromString(getParamString("wrapMode", "clampToEdge"));
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_inOffset = getParam<float4>("inOffset", float4(0.f, 0.f, 0.f, 0.f));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
  m_outOffset = getParam<float4>("outOffset", float4(0.f, 0.f, 0.f, 0.f));
}

float4 Image1D::getSample(
    const Geometry &g, const Ray &r, const UniformAttributeSet &instAttrV) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  const auto &ia = getUniformAttribute(instAttrV, m_inAttribute);
  auto av = linalg::mul(
                m_inTransform, ia ? *ia : g.getAttributeValue(m_inAttribute, r))
      + m_inOffset;

  const auto interp = getInterpolant(av.x, m_image->size(), true);
  const auto v0 = m_image->readAsAttributeValue(interp.lower, m_wrapMode);
  const auto v1 = m_image->readAsAttributeValue(interp.upper, m_wrapMode);
  const auto retval = m_linearFilter ? linalg::lerp(v0, v1, interp.frac)
                                     : (interp.frac < 0.5f ? v0 : v1);

  return linalg::mul(m_outTransform, retval) + m_outOffset;
}

} // namespace helide
