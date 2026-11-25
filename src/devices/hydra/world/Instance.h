// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "world/Group.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/utility/ChangeObserverPtr.h"

namespace anari_hydra {

using helium::float4;

// Per-instance uniform attribute overrides. Each ANARI instance may set `color`
// and `attribute0..3` as FLOAT32 / FLOAT32_VEC2 / VEC3 / VEC4 (missing lanes
// take the ANARI attribute default (0,0,0,1)) or as a per-transform ARRAY1D;
// these feed the prototype surfaces' material attribute reads *per instance* —
// the device authors them as instance-rate primvars on the (world, group)
// instancer, below the prototype's own per-vertex primvars in interpolation
// precedence (a geometry carrying its own `color` wins; the instance value
// fills in where absent — ANARI attribute precedence, proven in prototype 014).
// `has*` distinguishes an app-set value from the default so a value set on one
// instance does not silently override an unset sibling in the shared
// instance-rate array. `instance.id` is kept separately for frame picking.
struct InstanceOverrides
{
  bool hasColor{false};
  float4 color{0.f, 0.f, 0.f, 1.f};
  bool hasAttribute[4]{false, false, false, false};
  float4 attribute[4]{{0.f, 0.f, 0.f, 1.f},
      {0.f, 0.f, 0.f, 1.f},
      {0.f, 0.f, 0.f, 1.f},
      {0.f, 0.f, 0.f, 1.f}};
};

// ANARI instance -> instance transform(s) feeding a (world, group) instancer.
// `transform` is a FLOAT32_MAT4, a compact FLOAT32_MAT3x4 (the implied bottom
// row (0,0,0,1) is restored), or an ARRAY1D of either
// (KHR_INSTANCE_TRANSFORM_ARRAY): an array instance flattens to one entry per
// element on the group instancer, with `id` and the uniform attributes
// optionally arrays indexed by the same element (a scalar applies to every
// element; an `id` array is only honoured alongside a transform array, per the
// extension schema). The accessors are all per flattened element `i` in [0,
// transformCount()).
struct Instance : public Object
{
  Instance(HydraGlobalState *s);
  ~Instance() override = default;

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  Group *group() const
  {
    return m_group.ptr;
  }

  // Number of flattened transforms: 1 for a scalar transform, the array
  // length for an array transform.
  size_t transformCount() const;
  helium::mat4 transform(size_t i) const;
  uint32_t id(size_t i) const;
  InstanceOverrides overrides(size_t i) const;
  bool hasTransformArray() const
  {
    return bool(m_xfmArray);
  }

  uint64_t serial() const
  {
    return m_serial;
  }

 private:
  helium::IntrusivePtr<Group> m_group;
  // ChangeObserverPtr registers this instance as an observer of its arrays, so
  // an in-place map/unmap edit re-finalizes it and dirties the mirror.
  helium::ChangeObserverPtr<helium::Array1D> m_xfmArray;
  helium::ChangeObserverPtr<helium::Array1D> m_idArray;
  // Slots 0..3 = attribute0..3, slot 4 = color.
  helium::ChangeObserverPtr<helium::Array1D> m_attrArray[5];
  helium::mat4 m_xfm{linalg::identity};
  InstanceOverrides m_overrides;
  uint64_t m_serial{0};
  uint32_t m_id{~0u};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Instance *, ANARI_INSTANCE);
}
