// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sphere.h"

namespace helide {

Sphere::Sphere(HelideGlobalState *s) : Geometry(s)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_SPHERE_POINT);
}

void Sphere::commit()
{
  Geometry::commit();

  cleanup();

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_vertexAttributes[0] = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttributes[1] = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttributes[2] = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttributes[3] = getParamObject<Array1D>("vertex.attribute3");
  m_vertexAttributes[4] = getParamObject<Array1D>("vertex.color");

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on sphere geometry");
    return;
  }

  m_vertexPosition->addCommitObserver(this);
  if (m_vertexRadius)
    m_vertexRadius->addCommitObserver(this);
  if (m_index)
    m_index->addCommitObserver(this);

  m_globalRadius = getParam<float>("radius", 0.01f);

  const float *radius = nullptr;
  if (m_vertexRadius)
    radius = m_vertexRadius->beginAs<float>();

  const auto numSpheres = m_index ? m_index->size() : m_vertexPosition->size();

  auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
      RTC_BUFFER_TYPE_VERTEX,
      0,
      RTC_FORMAT_FLOAT4,
      sizeof(float4),
      numSpheres);

  if (m_index) {
    const auto *begin = m_index->beginAs<uint32_t>();
    const auto *end = m_index->endAs<uint32_t>();
    const auto *vertices = m_vertexPosition->beginAs<float3>();

    size_t sphereID = 0;
    std::transform(begin, end, vr, [&](uint32_t i) {
      const auto &v = vertices[i];
      const float r = radius ? radius[i] : m_globalRadius;
      return float4(v.x, v.y, v.z, r);
    });
  } else {
    const auto *begin = m_vertexPosition->beginAs<float3>();
    const auto *end = m_vertexPosition->endAs<float3>();

    size_t sphereID = 0;
    std::transform(begin, end, vr, [&](const float3 &v) {
      const float r = radius ? radius[sphereID++] : m_globalRadius;
      return float4(v.x, v.y, v.z, r);
    });
  }

  rtcCommitGeometry(embreeGeometry());
}

float4 Sphere::getAttributeValueAt(
    const Attribute &attr, const Ray &ray) const
{
  if (attr == Attribute::NONE)
    return Geometry::getAttributeValueAt(attr, ray);

  auto attrIdx = static_cast<int>(attr);
  auto *attributeArray = m_vertexAttributes[attrIdx].ptr;
  if (!attributeArray)
    return Geometry::getAttributeValueAt(attr, ray);

  return readAttributeArrayAt(attributeArray, ray.primID);
}

void Sphere::cleanup()
{
  if (m_index)
    m_index->removeCommitObserver(this);
  if (m_vertexPosition)
    m_vertexPosition->removeCommitObserver(this);
  if (m_vertexRadius)
    m_vertexRadius->removeCommitObserver(this);
}

} // namespace helide
