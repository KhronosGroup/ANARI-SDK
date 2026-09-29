// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sphere.h"

namespace helide {

Sphere::Sphere(HelideGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this), m_vertexRadius(this)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_SPHERE_POINT);
}

void Sphere::commitParameters()
{
  Geometry::commitParameters();
  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_vertexAttributes[0] = getParamObject<Array1D>("vertex.attribute0");
  m_vertexAttributes[1] = getParamObject<Array1D>("vertex.attribute1");
  m_vertexAttributes[2] = getParamObject<Array1D>("vertex.attribute2");
  m_vertexAttributes[3] = getParamObject<Array1D>("vertex.attribute3");
  m_vertexAttributes[4] = getParamObject<Array1D>("vertex.color");
}

void Sphere::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on sphere geometry");
    return;
  }

  m_globalRadius = getParam<float>("radius", 0.01f);

  m_attributeIndex.clear();
  std::vector<uint32_t> indices;
  const bool validIndex = !m_index || readIndices(*m_index, "sphere", indices);

  const auto *vertices = m_vertexPosition->beginAs<float3>();
  const size_t numVertices = m_vertexPosition->size();
  const Radii radii = readRadii(
      m_vertexRadius.get(), "vertex.radius", m_globalRadius, numVertices);

  size_t numSpheres = m_index ? indices.size() : numVertices;
  if (numVertices == 0 || !validIndex)
    numSpheres = 0;
  if (m_index && numVertices == 0 && !indices.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "sphere 'primitive.index' has %zu indices but there are no vertices",
        indices.size());
  }

  auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
      RTC_BUFFER_TYPE_VERTEX,
      0,
      RTC_FORMAT_FLOAT4,
      sizeof(float4),
      numSpheres);

  bool clamped = false;
  for (size_t i = 0; i < numSpheres; i++) {
    size_t v = m_index ? indices[i] : i;
    if (v >= numVertices) {
      clamped = true;
      v = numVertices - 1;
    }
    const auto &p = vertices[v];
    vr[i] = float4(p.x, p.y, p.z, radii[v]);
    if (m_index)
      m_attributeIndex.push_back(uint32_t(v));
  }

  if (clamped) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "sphere 'primitive.index' has indices past the %zu vertices; "
        "they use the last vertex",
        numVertices);
  }

  rtcCommitGeometry(embreeGeometry());
}

float4 Sphere::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (auto a = getRayAttribute(attr, ray); a.has_value())
    return *a;

  auto attrIdx = static_cast<int>(attr);
  auto *attributeArray = m_vertexAttributes[attrIdx].ptr;
  if (!attributeArray)
    return Geometry::getAttributeValue(attr, ray);

  const auto primID =
      m_attributeIndex.empty() ? ray.primID : m_attributeIndex[ray.primID];

  return readAttributeValue(attributeArray, primID);
}

} // namespace helide
