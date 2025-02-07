// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"

namespace helide {

struct Material : public Object
{
  Material(HelideGlobalState *s);
  ~Material() override = default;

  static Material *createInstance(
      std::string_view subtype, HelideGlobalState *s);

  void commitParameters() override;

  float4 color() const;
  Attribute colorAttribute() const;
  const Sampler *colorSampler() const;

  float opacity() const;
  Attribute opacityAttribute() const;
  const Sampler *opacitySampler() const;

  AlphaMode alphaMode() const;
  float alphaCutoff() const;

 protected:
  float4 m_color{1.f, 1.f, 1.f, 1.f};
  Attribute m_colorAttribute{Attribute::NONE};
  helium::IntrusivePtr<Sampler> m_colorSampler;

  float m_opacity{1.f};
  Attribute m_opacityAttribute{Attribute::NONE};
  helium::IntrusivePtr<Sampler> m_opacitySampler;
  float m_alphaCutoff{0.5f};

  AlphaMode m_alphaMode{AlphaMode::OPAQUE};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline float4 Material::color() const
{
  return m_color;
}

inline Attribute Material::colorAttribute() const
{
  return m_colorAttribute;
}

inline const Sampler *Material::colorSampler() const
{
  return m_colorSampler.ptr;
}

inline float Material::opacity() const
{
  return m_opacity;
}

inline Attribute Material::opacityAttribute() const
{
  return m_opacityAttribute;
}

inline const Sampler *Material::opacitySampler() const
{
  return m_opacitySampler.ptr;
}

inline AlphaMode Material::alphaMode() const
{
  return m_alphaMode;
}

inline float Material::alphaCutoff() const
{
  return m_alphaCutoff;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Material *, ANARI_MATERIAL);
