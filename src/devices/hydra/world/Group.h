// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "light/Light.h"
#include "scene/Bounds.h"
#include "surface/Surface.h"
#include "volume/Volume.h"
// helium
#include "helium/array/ObjectArray.h"
// std
#include <vector>

namespace anari_hydra {

// ANARI group -> one HdInstancer per (world, group) + prototype rprims.
// Collects the group's surfaces; a group's lights expand per instance into
// (instance, light) sprims; its volumes become prototype volume rprims on the
// same instancer.
struct Group : public Object
{
  Group(HydraGlobalState *s);
  ~Group() override = default;

  void commitParameters() override;
  void finalize() override;
  // ANARI bounds; `proxy` substitutes each volume's rasterized proxy box
  // (camera clipping, Volume::bounds).
  Bounds bounds(bool proxy = false) const;
  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  std::vector<Surface *> surfaces() const;
  std::vector<Light *> lights() const;
  std::vector<Volume *> volumes() const;

  uint64_t serial() const
  {
    return m_serial;
  }

 private:
  Bounds m_bounds;
  helium::IntrusivePtr<helium::ObjectArray> m_surface;
  helium::IntrusivePtr<helium::ObjectArray> m_light;
  helium::IntrusivePtr<helium::ObjectArray> m_volume;
  uint64_t m_serial{0};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Group *, ANARI_GROUP);
}
