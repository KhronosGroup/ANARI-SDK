// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Image3D.h"
#include "scene/surface/geometry/Geometry.h"

namespace helide {

Image3D::Image3D(HelideGlobalState *s) : Sampler(s) {}

bool Image3D::isValid() const
{
  return Sampler::isValid() && m_image;
}

void Image3D::commit()
{
  Sampler::commit();
  m_image = getParamObject<Array3D>("image");
  m_inAttribute =
      attributeFromString(getParamString("inAttribute", "attribute0"));
  m_linearFilter = getParamString("filter", "linear") != "nearest";
  m_wrapMode1 = wrapModeFromString(getParamString("wrapMode1", "clampToEdge"));
  m_wrapMode2 = wrapModeFromString(getParamString("wrapMode2", "clampToEdge"));
  m_wrapMode3 = wrapModeFromString(getParamString("wrapMode3", "clampToEdge"));
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
}

float4 Image3D::getSample(const Geometry &g, const Ray &r) const
{
  if (m_inAttribute == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  auto av = linalg::mul(m_inTransform, g.getAttributeValue(m_inAttribute, r));

  const auto interp_x = getInterpolant(av.x, m_image->size().x);
  const auto interp_y = getInterpolant(av.y, m_image->size().y);
  const auto interp_z = getInterpolant(av.z, m_image->size().z);

  const auto v000 = m_image->readAsAttributeValue(
      {interp_x.lower, interp_y.lower, interp_z.lower},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v001 = m_image->readAsAttributeValue(
      {interp_x.lower, interp_y.lower, interp_z.upper},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v010 = m_image->readAsAttributeValue(
      {interp_x.lower, interp_y.upper, interp_z.lower},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v011 = m_image->readAsAttributeValue(
      {interp_x.lower, interp_y.upper, interp_z.upper},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v100 = m_image->readAsAttributeValue(
      {interp_x.upper, interp_y.lower, interp_z.lower},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v101 = m_image->readAsAttributeValue(
      {interp_x.upper, interp_y.lower, interp_z.upper},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v110 = m_image->readAsAttributeValue(
      {interp_x.upper, interp_y.upper, interp_z.lower},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);
  const auto v111 = m_image->readAsAttributeValue(
      {interp_x.upper, interp_y.upper, interp_z.upper},
      m_wrapMode1,
      m_wrapMode2,
      m_wrapMode3);

  const auto v00 = m_linearFilter ? linalg::lerp(v000, v001, interp_z.frac)
                                  : (interp_z.frac < 0.5f ? v000 : v001);
  const auto v01 = m_linearFilter ? linalg::lerp(v010, v011, interp_z.frac)
                                  : (interp_z.frac < 0.5f ? v010 : v011);
  const auto v10 = m_linearFilter ? linalg::lerp(v100, v101, interp_z.frac)
                                  : (interp_z.frac < 0.5f ? v100 : v101);
  const auto v11 = m_linearFilter ? linalg::lerp(v110, v111, interp_z.frac)
                                  : (interp_z.frac < 0.5f ? v110 : v111);

  const auto v0 = m_linearFilter ? linalg::lerp(v00, v01, interp_y.frac)
                                 : (interp_y.frac < 0.5f ? v00 : v01);
  const auto v1 = m_linearFilter ? linalg::lerp(v10, v11, interp_y.frac)
                                 : (interp_y.frac < 0.5f ? v10 : v11);

  const auto retval = m_linearFilter ? linalg::lerp(v0, v1, interp_x.frac)
                                     : (interp_x.frac < 0.5f ? v0 : v1);

  return linalg::mul(m_outTransform, retval);
}

} // namespace helide
