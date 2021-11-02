// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"

namespace anari {
namespace example_device {

struct World : public IntersectableObject<IntersectablePtr<Instance>>
{
  World();

  void commit() override;

  IntrusivePtr<ObjectArray> m_instanceData;
  Span<Instance *> m_instances;
  IntrusivePtr<Group> m_zeroGroup;
  IntrusivePtr<Instance> m_zeroInstance;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PotentialHit closestHit(Ray ray, const World &world)
{
  auto hit = world.intersect(ray, 0);
  if (hitGeometry(hit)) {
    GeometryInfo &gi = getGeometryInfo(hit);
    gi.normal = normalize(gi.normal);
    const bool forwardNormal = dot(-ray.dir, gi.normal) >= 0.f;
    if (!forwardNormal)
      gi.normal = -gi.normal;
  }
  return hit;
}

inline bool isOccluded(Ray ray, const World &world)
{
  auto hit = world.intersect(ray, 0);
  return hit.has_value();
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::World *, ANARI_WORLD);

} // namespace anari
