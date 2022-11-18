// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Quad.h"
// std
#include <numeric>

namespace helide {

Quad::Quad(HelideGlobalState *s) : Geometry(s)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_QUAD);
}

void Quad::commit()
{
  Geometry::commit();

  cleanup();

  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'vertex.position' on quad geometry");
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
        RTC_FORMAT_UINT4,
        m_index->dataAs<uint4>(),
        0,
        sizeof(uint4),
        m_index->size());
  } else {
    const auto numQuads = m_vertexPosition->size() / 4;
    auto *vr = (uint32_t *)rtcSetNewGeometryBuffer(embreeGeometry(),
        RTC_BUFFER_TYPE_INDEX,
        0,
        RTC_FORMAT_UINT4,
        sizeof(uint4),
        numQuads);
    std::iota(vr, vr + (numQuads * 4), 0);
  }

  rtcCommitGeometry(embreeGeometry());
}

void Quad::cleanup()
{
  if (m_index)
    m_index->removeCommitObserver(this);
  if (m_vertexPosition)
    m_vertexPosition->removeCommitObserver(this);
}

} // namespace helide
