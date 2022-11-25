// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "sampler/Sampler.h"

namespace helide {

struct Material : public Object
{
  static size_t objectCount();

  Material(HelideGlobalState *s);
  ~Material() override;

  static Material *createInstance(
      std::string_view subtype, HelideGlobalState *s);

  float3 color() const;
  float opacity() const;

 protected:
  float3 m_color{1.f, 1.f, 1.f};
  float m_opacity{1.f};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline float3 Material::color() const
{
  return m_color;
}

inline float Material::opacity() const
{
  return m_opacity;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Material *, ANARI_MATERIAL);
