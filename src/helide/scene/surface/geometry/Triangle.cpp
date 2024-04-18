// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Triangle.h"
// std
#include <numeric>

namespace helide {

Triangle::Triangle(HelideGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
}

void Triangle::commit()
{
  Geometry::commit();

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexAttributes[0] = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttributes[1] = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttributes[2] = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttributes[3] = getParamObject<Array1D>("vertex.attribute3");
  m_vertexAttributes[4] = getParamObject<Array1D>("vertex.color");

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on triangle geometry");
    return;
  }

  rtcSetSharedGeometryBuffer(embreeGeometry(),
      RTC_BUFFER_TYPE_VERTEX,
      0,
      RTC_FORMAT_FLOAT3,
      m_vertexPosition->dataAs<float3>(),
      0,
      sizeof(float3),
      m_vertexPosition->size());

  if (m_index) {
    rtcSetSharedGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT3,
        m_index->dataAs<uint3>(),
        0,
        sizeof(uint3),
        m_index->size());
  } else {
    const auto numTris = m_vertexPosition->size() / 3;
    auto *vr = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT3,
        sizeof(uint3),
        numTris);
    std::iota(vr, vr + (numTris * 3), 0);
  }

  rtcCommitGeometry(embreeGeometry());
}

float4 Triangle::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (attr == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  auto attrIdx = static_cast<int>(attr);
  auto *attributeArray = m_vertexAttributes[attrIdx].ptr;
  if (!attributeArray)
    return Geometry::getAttributeValue(attr, ray);

  const float3 uvw(1.0f - ray.u - ray.v, ray.u, ray.v);

  auto idx = m_index ? *(m_index->dataAs<uint3>() + ray.primID)
                     : 3 * ray.primID + uint3(0, 1, 2);

  auto a = readAttributeValue(attributeArray, idx.x);
  auto b = readAttributeValue(attributeArray, idx.y);
  auto c = readAttributeValue(attributeArray, idx.z);

  return uvw.x * a + uvw.y * b + uvw.z * c;
}

} // namespace helide
