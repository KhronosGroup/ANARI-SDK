// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry/Geometry.h"
#include "material/Material.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Surface : public Object
{
  Surface(HeCoreDeviceGlobalState *s);
  ~Surface() override = default;

  void commitParameters() override;

  uint32_t id() const;
  const Geometry *geometry() const;
  const Material *material() const;

  bool isValid() const override;

 private:
  uint32_t m_id{~0u};
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Surface *, ANARI_SURFACE);
