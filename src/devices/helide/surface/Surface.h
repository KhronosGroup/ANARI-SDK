// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry/Geometry.h"
#include "material/Material.h"

namespace helide {

struct Instance;

struct Surface : public Object
{
  Surface(HelideGlobalState *s);
  ~Surface() override = default;

  void commitParameters() override;
  void finalize() override;
  void markFinalized() override;
  bool isValid() const override;

  uint32_t id() const;
  const Geometry *geometry() const;
  const Material *material() const;

  float4 getSurfaceColor(
      const Ray &ray, const UniformAttributeSet &instAttrV) const;
  float getSurfaceOpacity(
      const Ray &ray, const UniformAttributeSet &instAttrV) const;

  float adjustedAlpha(float a) const;

 private:
  uint32_t m_id{~0u};
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Surface::id() const
{
  return m_id;
}

inline float Surface::adjustedAlpha(float a) const
{
  if (!material())
    return 0.f;

  return adjustOpacityFromMode(
      a, material()->alphaCutoff(), material()->alphaMode());
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Surface *, ANARI_SURFACE);
