// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image1D.h"
#include "scene/surface/geometry/Geometry.h"

namespace helide {

Image1D::Image1D(HelideGlobalState *s) : Sampler(s) {}

bool Image1D::isValid() const
{
  return Sampler::isValid() && m_image;
}

void Image1D::commit()
{
  Sampler::commit();
  m_image = getParamObject<Array1D>("image");
  m_inAttribute =
      attributeFromString(getParamString("inAttribute", "attribute0"));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode = wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
}

float4 Image1D::getSample(const Geometry &g, const Ray &r) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  auto av = linalg::mul(m_inTransform, g.getAttributeValue(m_inAttribute, r));

  const auto interp = getInterpolant(av.x, m_image->size());
  const auto v0 = m_image->readAsAttributeValue(interp.lower, m_wrapMode);
  const auto v1 = m_image->readAsAttributeValue(interp.upper, m_wrapMode);
  const auto retval = m_linearFilter ? linalg::lerp(v0, v1, interp.frac)
                                     : (interp.frac < 0.5f ? v0 : v1);

  return linalg::mul(m_outTransform, retval);
}

} // namespace helide
