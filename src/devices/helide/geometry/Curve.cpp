// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Curve.h"
// std
#include <algorithm>
#include <numeric>

namespace helide {

Curve::Curve(HelideGlobalState *s)
    : Geometry(s), m_index(this), m_vertexPosition(this), m_vertexRadius(this)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_ROUND_LINEAR_CURVE);
}

void Curve::commitParameters()
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

void Curve::finalize()
{
  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on curve geometry");
    return;
  }

  m_globalRadius = getParam<float>("radius", 1.f);

  m_segmentStarts.clear();
  const bool validIndex =
      !m_index || readIndices(*m_index, "curve", m_segmentStarts);

  const auto *vertices = m_vertexPosition->beginAs<float3>();
  const size_t numVertices = m_vertexPosition->size();
  const Radii radii = readRadii(
      m_vertexRadius.get(), "vertex.radius", m_globalRadius, numVertices);

  // a segment spans two vertices
  if (numVertices < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "curve geometry needs at least 2 vertices, has %zu",
        numVertices);
  }
  size_t numSegments = m_index ? m_segmentStarts.size() : numVertices - 1;
  if (numVertices < 2 || !validIndex) {
    numSegments = 0;
    m_segmentStarts.clear();
  }

  auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
      RTC_BUFFER_TYPE_VERTEX,
      0,
      RTC_FORMAT_FLOAT4,
      sizeof(float4),
      numVertices);
  for (size_t i = 0; i < numVertices; i++)
    vr[i] = float4(vertices[i], radii[i]);

  if (m_index) {
    // segment starts past the second-to-last vertex would end past the last
    // (with fewer than 2 vertices there are no segments to clamp)
    const auto lastStart = numVertices < 2 ? 0u : uint32_t(numVertices - 2);
    bool clamped = false;
    for (auto &start : m_segmentStarts) {
      clamped |= start > lastStart;
      start = std::min(start, lastStart);
    }
    if (clamped) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "curve 'primitive.index' has segments ending past the %zu "
          "vertices; they use the last segment",
          numVertices);
    }
    rtcSetSharedGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT,
        m_segmentStarts.data(),
        0,
        sizeof(uint32_t),
        numSegments);
  } else {
    auto *idx = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT,
        sizeof(uint32_t),
        numSegments);
    std::iota(idx, idx + numSegments, 0);
  }

  rtcCommitGeometry(embreeGeometry());
}

float4 Curve::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (auto a = getRayAttribute(attr, ray); a.has_value())
    return *a;

  auto attrIdx = static_cast<int>(attr);
  auto *attributeArray = m_vertexAttributes[attrIdx].ptr;
  if (!attributeArray)
    return Geometry::getAttributeValue(attr, ray);

  const auto idx = m_index ? m_segmentStarts[ray.primID] : ray.primID;

  auto a = readAttributeValue(attributeArray, idx + 0);
  auto b = readAttributeValue(attributeArray, idx + 1);

  return a + (b - a) * ray.u;
}

} // namespace helide
