// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"

namespace helide {

struct Material : public Object
{
  Material(HelideGlobalState *s);
  ~Material() override;

  static Material *createInstance(
      std::string_view subtype, HelideGlobalState *s);

  float3 color() const;
  Attribute colorAttribute() const;
  const Sampler *colorSampler() const;

 protected:
  Attribute attributeFromString(const std::string &str) const;

  float3 m_color{1.f, 1.f, 1.f};
  Attribute m_colorAttribute{Attribute::NONE};
  helium::IntrusivePtr<Sampler> m_colorSampler;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline float3 Material::color() const
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

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Material *, ANARI_MATERIAL);
