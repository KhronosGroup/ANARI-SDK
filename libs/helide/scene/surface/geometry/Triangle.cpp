// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Triangle.h"
// std
#include <numeric>

namespace helide {

Triangle::Triangle(HelideGlobalState *s) : Geometry(s)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
}

void Triangle::commit()
{
  Geometry::commit();

  cleanup();

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on triangle geometry");
    return;
  }

  m_vertexPosition->addCommitObserver(this);
  if (m_index)
    m_index->addCommitObserver(this);

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

void Triangle::cleanup()
{
  if (m_index)
    m_index->removeCommitObserver(this);
  if (m_vertexPosition)
    m_vertexPosition->removeCommitObserver(this);
}

} // namespace helide
