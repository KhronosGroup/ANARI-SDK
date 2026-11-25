// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Matte.h"
// std
#include <algorithm>

namespace helide_gpu {

Matte::Matte(HelideGPUDeviceGlobalState *s, bool physicallyBased)
    : Material(s), m_physicallyBased(physicallyBased)
{}

void Matte::commitParameters()
{
  m_colorAttribute.clear();
  m_colorSampler = nullptr;
  m_color = vec3(1.f);
  m_opacity = 1.f;
  m_opacitySampler = nullptr;
  m_alphaMode = 0;
  m_alphaCutoff = 0.5f;
  m_metallic = 0.f;
  m_roughness = 1.f;
  m_metallicAttribute.clear();
  m_roughnessAttribute.clear();
  m_metallicSampler = nullptr;
  m_roughnessSampler = nullptr;
  m_ior = 1.5f;
  m_emissive = vec3(0.f);
  m_emissiveSampler = nullptr;
  m_normalSampler = nullptr;

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
  m_opacitySampler = getParamObject<Sampler>("opacity");
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

  // physicallyBased's other parameters: metallic and roughness may also be a
  // sampler or name a geometry attribute, emissive may be a sampler
  if (m_physicallyBased) {
    m_metallic = std::clamp(getParam<float>("metallic", 1.f), 0.f, 1.f);
    m_metallicAttribute = getParamString("metallic", "");
    m_metallicSampler = getParamObject<Sampler>("metallic");
    m_roughness = std::clamp(getParam<float>("roughness", 1.f), 0.f, 1.f);
    m_roughnessAttribute = getParamString("roughness", "");
    m_roughnessSampler = getParamObject<Sampler>("roughness");
    m_ior = std::max(getParam<float>("ior", 1.5f), 1.f);
    m_emissive = getParam<vec3>("emissive", vec3(0.f));
    m_emissiveSampler = getParamObject<Sampler>("emissive");

    // normal: a tangent-space normal map, which needs an image to give the
    // texture coordinates it maps with (as in Storm)
    m_normalSampler = getParamObject<Sampler>("normal");
    if (m_normalSampler && m_normalSampler->samplerMode() != 2) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "physicallyBased 'normal' maps only from an image sampler, ignored");
      m_normalSampler = nullptr;
    }
  }
}

} // namespace helide_gpu
