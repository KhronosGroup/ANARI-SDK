// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry/Geometry.h"
#include "material/Material.h"

namespace halcyon {

struct Surface : public Object
{
  Surface(HalcyonDeviceGlobalState *s);
  ~Surface() override = default;

  void commitParameters() override;
  void finalize() override;
  void markFinalized() override;
  // A missing or invalid material is valid if the device allows it
  bool isValid() const override;
  // 'visible': invisible surfaces are left out of their group's BLS
  bool isVisible() const;

  uint32_t id() const;
  const Geometry *geometry() const;
  const Material *material() const;

 private:
  uint32_t m_id{~0u};
  bool m_visible{true};
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
  // The material again if its validity can change (a 'materialx'
  // material's), so that its finalize() finalizes this surface again
  helium::ChangeObserverPtr<Material> m_observedMaterial{this};
  // Whether finalize() follows a commit of this surface, and isValid() as
  // finalize() last saw it: a finalize() only for the material's sake asks
  // for a scene rebuild only if it changed (invalid surfaces are left out)
  bool m_committed{false};
  bool m_wasValid{false};
  bool m_sceneChanged{true};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Surface::id() const
{
  return m_id;
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Surface *, ANARI_SURFACE);
