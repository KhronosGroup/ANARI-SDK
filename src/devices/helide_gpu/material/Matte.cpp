// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"

namespace helide_gpu {

Matte::Matte(HelideGPUDeviceGlobalState *s) : Material(s) {}

void Matte::commitParameters()
{
  m_colorAttribute.clear();
  m_colorSampler = nullptr;
  m_color = vec3(1.f);
  m_opacity = 1.f;
  m_alphaMode = 0;
  m_alphaCutoff = 0.5f;

  // "color" (matte subtype): accept sampler, string attribute name, or vec3
  {
    auto sampler = getParamObject<Sampler>("color");
    if (sampler) {
      m_colorSampler = sampler;
    } else {
      const auto s = getParamString("color", "");
      if (!s.empty()) {
        m_colorAttribute = s;
      } else {
        m_color = getParam<vec3>("color", m_color);
      }
    }
  }

  // Opacity and alpha mode
  m_opacity = getParam<float>("opacity", 1.f);
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
  {
    const auto mode = getParamString("alphaMode", "opaque");
    if (mode == "blend")
      m_alphaMode = 1;
    else if (mode == "mask")
      m_alphaMode = 2;
    else
      m_alphaMode = 0;
  }

  // "baseColor" overrides (physicallyBased subtype)
  {
    auto sampler = getParamObject<Sampler>("baseColor");
    if (sampler) {
      m_colorSampler = sampler;
      m_colorAttribute.clear();
      m_color = vec3(1.f);
    } else {
      const auto s = getParamString("baseColor", "");
      if (!s.empty()) {
        m_colorAttribute = s;
        m_colorSampler = nullptr;
        m_color = vec3(1.f);
      } else {
        m_color = getParam<vec3>("baseColor", m_color);
      }
    }
  }
}

} // namespace helide_gpu
