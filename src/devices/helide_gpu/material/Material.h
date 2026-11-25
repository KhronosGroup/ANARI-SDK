// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"
// std
#include <string>

namespace helide_gpu {

struct Material : public Object
{
  Material(HelideGPUDeviceGlobalState *s);
  ~Material() override = default;
  static Material *createInstance(
      std::string_view subtype, HelideGPUDeviceGlobalState *s);

  vec3 color() const;
  const std::string &colorAttribute() const; // empty = use uniform color
  const Sampler *colorSampler() const;

  float opacity() const;
  const Sampler *opacitySampler() const; // first output component
  int32_t alphaMode() const;
  float alphaCutoff() const;

  // Storm's UsdPreviewSurface model (metallic workflow); matte is metallic 0,
  // roughness 1
  float metallic() const;
  float roughness() const;
  // geometry attributes for metallic and roughness, empty = uniform value
  const std::string &metallicAttribute() const;
  const std::string &roughnessAttribute() const;
  // samplers for metallic and roughness (their first output component), which
  // take precedence over the attribute and the value
  const Sampler *metallicSampler() const;
  const Sampler *roughnessSampler() const;
  float ior() const;
  vec3 emissive() const;
  const Sampler *emissiveSampler() const;
  // tangent-space normal map, image samplers only
  const Sampler *normalSampler() const;

 protected:
  vec3 m_color{1.f, 1.f, 1.f};
  std::string m_colorAttribute;
  helium::IntrusivePtr<Sampler> m_colorSampler;
  float m_opacity{1.f};
  helium::IntrusivePtr<Sampler> m_opacitySampler;
  int32_t m_alphaMode{0}; // 0=opaque, 1=blend, 2=mask
  float m_alphaCutoff{0.5f};
  float m_metallic{0.f};
  float m_roughness{1.f};
  std::string m_metallicAttribute;
  std::string m_roughnessAttribute;
  helium::IntrusivePtr<Sampler> m_metallicSampler;
  helium::IntrusivePtr<Sampler> m_roughnessSampler;
  float m_ior{1.5f};
  vec3 m_emissive{0.f};
  helium::IntrusivePtr<Sampler> m_emissiveSampler;
  helium::IntrusivePtr<Sampler> m_normalSampler;
};

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Material *, ANARI_MATERIAL);
