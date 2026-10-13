// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/Bounds.h"
#include "light/Light.h"
#include "scene/Mirror.h"
#include "world/Instance.h"
// helium
#include "helium/array/ObjectArray.h"
// std
#include <memory>
#include <vector>

namespace anari_hydra {

// One retained mirror per ANARI world (SPEC §4.4). Holds world-direct surfaces
// and instances; gatherRenderables() flattens them to (path-key, surface,
// world-space transform) tuples for the mirror to author.
struct World : public Object
{
  World(HydraGlobalState *s);
  ~World() override = default;

  void commitParameters() override;
  void finalize() override;
  // ANARI bounds; `proxy` substitutes each volume's rasterized proxy box, for
  // fitting camera clipping planes (Volume::bounds).
  Bounds bounds(bool proxy = false) const;
  bool getProperty(const std::string_view &name, ANARIDataType type,
      void *ptr, uint64_t size, uint32_t flags) override;

  Mirror *mirror() const { return m_mirror.get(); }
  uint64_t serial() const { return m_serial; }

  // A world-direct surface (identity transform, no instancer, SPEC §4.3).
  struct Renderable
  {
    uint64_t surfaceSerial{0};
    Surface *surface{nullptr};
  };

  std::vector<Renderable> gatherRenderables() const;

  // A group instanced N times: the group's surfaces become prototype rprims
  // authored once, drawn once per instance transform via one HdInstancer
  // (SPEC §4.3). Every vector is parallel to `transforms`, one entry per
  // *flattened* transform: a scalar-transform instance contributes one entry,
  // an array-transform instance (KHR_INSTANCE_TRANSFORM_ARRAY) one per element.
  // `instanceSerials` + `elements` key a group's per-instance light expansion
  // (SPEC §6.8); `elements[i]` is -1 for a scalar transform, else the array
  // index.
  struct InstancedGroup
  {
    Group *group{nullptr};
    std::vector<helium::mat4> transforms;
    std::vector<uint64_t> instanceSerials;
    std::vector<int> elements;
    // Per-instance attribute overrides parallel to `transforms` (SPEC §6.8):
    // instance.color / attribute0..3 feeding the prototype material reads.
    std::vector<InstanceOverrides> overrides;
    std::vector<uint32_t> ids;
  };

  std::vector<InstancedGroup> gatherInstancedGroups() const;

  // World-direct lights (SPEC §6.6). Instanced-group lights expand separately
  // via gatherInstancedGroups() + Group::lights() (SPEC §6.8).
  std::vector<Light *> gatherLights() const;

  // World-direct volumes (identity transform, no instancer, SPEC §7).
  std::vector<Volume *> gatherVolumes() const;

  // True when the world contributes any light — world-direct or an instanced
  // group carrying lights (SPEC §6.6/§6.8) — so the fallback headlight drops
  // even when only instanced-group lights exist.
  bool hasAnyLight() const;

 private:
  Bounds m_bounds;
  helium::IntrusivePtr<helium::ObjectArray> m_instance;
  helium::IntrusivePtr<helium::ObjectArray> m_surface;   // world-direct
  helium::IntrusivePtr<helium::ObjectArray> m_light;
  helium::IntrusivePtr<helium::ObjectArray> m_volume;
  std::unique_ptr<Mirror> m_mirror;
  uint64_t m_serial{0};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::World *, ANARI_WORLD);
}
