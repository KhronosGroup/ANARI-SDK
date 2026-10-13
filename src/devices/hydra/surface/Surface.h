// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "geometry/Geometry.h"
#include "material/Material.h"

namespace anari_hydra {

// ANARI surface -> one rprim; its geometry supplies bulk data sources, its
// material becomes a material sprim referenced by materialId.
struct Surface : public Object
{
  Surface(HydraGlobalState *s);
  ~Surface() override = default;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  Geometry *geometry() const
  {
    return m_geometry.ptr;
  }
  Material *material() const
  {
    return m_material.ptr;
  }

  // Device-assigned serial for the prim-path taxonomy.
  uint32_t id() const
  {
    return m_id;
  }
  uint64_t serial() const
  {
    return m_serial;
  }

 private:
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
  uint64_t m_serial{0};
  uint32_t m_id{~0u};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Surface *, ANARI_SURFACE);
}
