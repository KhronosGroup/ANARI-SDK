// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Cone.h"
// std
#include <numeric>

namespace helide {

Cone::Cone(HelideGlobalState *s) : Geometry(s)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_CONE_LINEAR_CURVE);
}

void Cone::commit()
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
        "missing required parameter 'vertex.position' on cone geometry");
    return;
  }

  if (m_index)
    m_index->addCommitObserver(this);
  m_vertexPosition->addCommitObserver(this);

  const float *radius =
      m_vertexRadius ? m_vertexRadius->beginAs<float>() : nullptr;
  m_globalRadius = getParam<float>("radius", 1.f);

  const auto numCones =
      m_index ? m_index->size() : m_vertexPosition->size() / 2;

  {
    auto *vr = (float4 *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_VERTEX,
        0,
        RTC_FORMAT_FLOAT4,
        sizeof(float4),
        numCones * 2);

    if (m_index) {
      const auto *begin = m_index->beginAs<uint2>();
      const auto *end = m_index->endAs<uint2>();
      const auto *v = m_vertexPosition->beginAs<float3>();
      uint32_t cID = 0;
      std::for_each(begin, end, [&](const uint2 &idx) {
        vr[cID + 0] = float4(v[idx.x], radius ? radius[idx.x] : m_globalRadius);
        vr[cID + 1] = float4(v[idx.y], radius ? radius[idx.y] : m_globalRadius);
        cID += 2;
      });
    } else {
      const auto *begin = m_vertexPosition->beginAs<float3>();
      const auto *end = m_vertexPosition->endAs<float3>();
      uint32_t rID = 0;
      std::transform(begin, end, vr, [&](const float3 &v) {
        return float4(v, radius ? radius[rID++] : m_globalRadius);
      });
    }
  }

  {
    auto *idx = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT,
        sizeof(uint32_t),
        numCones);
    std::iota(idx, idx + numCones, 0);
    std::transform(idx, idx + numCones, idx, [](auto &i) { return i * 2; });
  }

  rtcCommitGeometry(embreeGeometry());
}

float4 Cone::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (attr == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  auto attrIdx = static_cast<int>(attr);
  auto *attributeArray = m_vertexAttributes[attrIdx].ptr;
  if (!attributeArray)
    return Geometry::getAttributeValue(attr, ray);

  auto idx = m_index ? *(m_index->dataAs<uint2>() + ray.primID)
                     : 2 * ray.primID + uint2(0, 1);

  auto a = readAttributeValue(attributeArray, idx.x);
  auto b = readAttributeValue(attributeArray, idx.y);

  return a + (b - a) * ray.u;
}

void Cone::cleanup()
{
  if (m_index)
    m_index->removeCommitObserver(this);
  if (m_vertexPosition)
    m_vertexPosition->removeCommitObserver(this);
}

} // namespace helide
