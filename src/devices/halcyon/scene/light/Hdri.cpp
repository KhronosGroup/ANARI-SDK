// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Hdri.h"
#include "HdriDistribution.h"

#include "Light_ispc.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>

namespace halcyon {

Hdri::Hdri(HalcyonDeviceGlobalState *s) : Light(s) {}

bool Hdri::frame(const float3 &up,
    const float3 &direction,
    float3 &left,
    float3 &upOut,
    float3 &directionOut)
{
  upOut = normalizedOrZero(up);
  const float3 d = normalizedOrZero(direction);
  if (upOut == float3(0.f) || d == float3(0.f))
    return false;
  const float3 perpendicular = d - linalg::dot(d, upOut) * upOut;
  if (!(linalg::length(perpendicular) > 1e-6f))
    return false;
  directionOut = normalizedOrZero(perpendicular);
  left = linalg::cross(upOut, directionOut);
  return true;
}

void Hdri::commitParameters()
{
  commitCommonParameters();
  m_up = getParam<float3>("up", float3(0.f, 0.f, 1.f));
  m_direction = getParam<float3>("direction", float3(1.f, 0.f, 0.f));
  const float scale = getParam<float>("scale", 1.f);
  m_scale = std::isfinite(scale) ? std::max(scale, 0.f) : 0.f;
  m_radiance = getParamObject<Array2D>("radiance");
  const std::string layout = getParamString("layout", "equirectangular");
  if (layout != "equirectangular") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'layout' \"%s\" is not supported; using "
        "\"equirectangular\"",
        layout.c_str());
  }
}

void Hdri::finalize()
{
  m_valid = false;
  m_emits = false;
  m_texels.clear();
  m_texelWeights.clear();
  m_conditionalCdf.clear();
  m_marginalCdf.clear();
  m_weightSum = 0.f;
  m_width = 0;
  m_height = 0;
  m_emission = m_color * m_scale;

  float3 left, up, direction;
  if (!frame(m_up, m_direction, left, up, direction)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'up' and 'direction' must be non-zero, finite and not "
        "parallel; the light is invalid");
    return;
  }

  if (!m_radiance) {
    reportMessage(ANARI_SEVERITY_WARNING,
        hasParam("radiance")
            ? "hdri light 'radiance' must be an ARRAY2D; the light is invalid"
            : "hdri light 'radiance' is missing; the light is invalid");
    return;
  }
  if (m_radiance->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'radiance' must have FLOAT32_VEC3 elements (got %s); the "
        "light is invalid",
        anari::toString(m_radiance->elementType()));
    return;
  }
  const size_t width = m_radiance->size().x;
  const size_t height = m_radiance->size().y;
  if (width == 0 || height == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'radiance' is empty; the light is invalid");
    return;
  }

  // the rows reversed into Typhoon's order (row 0 towards 'up')
  const float *src = static_cast<const float *>(m_radiance->data());
  m_texels.resize(width * height * 3);
  for (size_t y = 0; y < height; y++) {
    std::memcpy(m_texels.data() + y * width * 3,
        src + (height - 1 - y) * width * 3,
        width * 3 * sizeof(float));
  }
  bool clamped = false;
  for (auto &v : m_texels) {
    if (!(v >= 0.f) || !std::isfinite(v)) {
      v = std::isfinite(v) ? std::max(v, 0.f) : 0.f;
      clamped = true;
    }
    m_emits = m_emits || v > 0.f;
  }
  if (clamped) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hdri light 'radiance' has negative or non-finite values; they are "
        "taken as 0");
  }

  m_width = uint32_t(width);
  m_height = uint32_t(height);
  buildHdriDistribution(m_texels,
      int(width),
      int(height),
      m_texelWeights,
      m_conditionalCdf,
      m_marginalCdf,
      m_weightSum);
  m_valid = true;
}

bool Hdri::isValid() const
{
  return m_valid;
}

bool Hdri::writeWorldRecord(void *record, const mat4 &xfm) const
{
  // one that emits nothing is kept only for camera rays to see it black,
  // as Typhoon's camera rays see a dome without radiance
  if ((!m_emits || m_emission == float3(0.f)) && !m_visible)
    return false;
  float3 left, up, direction;
  if (!frame(linalg::mul(xfm, float4(m_up, 0.f)).xyz(),
          linalg::mul(xfm, float4(m_direction, 0.f)).xyz(),
          left,
          up,
          direction))
    return false;
  const bool distribution = m_weightSum > 0.f;
  ispc::Light_setHdri(record,
      &left.x,
      &up.x,
      &direction.x,
      m_texels.data(),
      m_width,
      m_height,
      distribution ? m_texelWeights.data() : nullptr,
      distribution ? m_conditionalCdf.data() : nullptr,
      distribution ? m_marginalCdf.data() : nullptr,
      m_weightSum,
      &m_emission.x,
      m_visible);
  return true;
}

} // namespace halcyon
