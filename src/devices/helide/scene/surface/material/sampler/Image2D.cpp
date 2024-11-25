// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image2D.h"
#include "scene/surface/geometry/Geometry.h"

namespace helide {

Image2D::Image2D(HelideGlobalState *s) : Sampler(s) {}

bool Image2D::isValid() const
{
  return Sampler::isValid() && m_image;
}

void Image2D::commit()
{
  Sampler::commit();
  m_image = getParamObject<Array2D>("image");
  m_inAttribute =
      attributeFromString(getParamString("inAttribute", "attribute0"));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode1 = wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_wrapMode2 = wrapModeFromString(getParamString("wrapMode2", "clampToEdge"));
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_inOffset = getParam<float4>("inOffset", float4(0.f, 0.f, 0.f, 0.f));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
  m_outOffset = getParam<float4>("outOffset", float4(0.f, 0.f, 0.f, 0.f));
}

float4 Image2D::getSample(
    const Geometry &g, const Ray &r, const UniformAttributeSet &instAttrV) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  const auto &ia = getUniformAttribute(instAttrV, m_inAttribute);
  auto av = linalg::mul(
                m_inTransform, ia ? *ia : g.getAttributeValue(m_inAttribute, r))
      + m_inOffset;

  const auto interp_x = getInterpolant(av.x, m_image->size().x, true);
  const auto interp_y = getInterpolant(av.y, m_image->size().y, true);
  const auto v00 = m_image->readAsAttributeValue(
      {interp_x.lower, interp_y.lower}, m_wrapMode1, m_wrapMode2);
  const auto v01 = m_image->readAsAttributeValue(
      {interp_x.lower, interp_y.upper}, m_wrapMode1, m_wrapMode2);
  const auto v10 = m_image->readAsAttributeValue(
      {interp_x.upper, interp_y.lower}, m_wrapMode1, m_wrapMode2);
  const auto v11 = m_image->readAsAttributeValue(
      {interp_x.upper, interp_y.upper}, m_wrapMode1, m_wrapMode2);

  const auto v0 = m_linearFilter ? linalg::lerp(v00, v01, interp_y.frac)
                                 : (interp_y.frac < 0.5f ? v00 : v01);
  const auto v1 = m_linearFilter ? linalg::lerp(v10, v11, interp_y.frac)
                                 : (interp_y.frac < 0.5f ? v10 : v11);

  const auto retval = m_linearFilter ? linalg::lerp(v0, v1, interp_x.frac)
                                     : (interp_x.frac < 0.5f ? v0 : v1);

  return linalg::mul(m_outTransform, retval) + m_outOffset;
}

} // namespace helide
