// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Group.h"

namespace anari {
namespace example_device {

struct Instance : public SceneObject
{
  Instance();

  void commit() override;

  PotentialHit intersect(const Ray &ray, size_t) const override;

 private:
  std::optional<mat4> m_xfm;
  std::optional<mat4> m_invXfm;
  std::optional<mat3> m_normXfm;
  IntrusivePtr<Group> m_group;
};

// Inlined defintions /////////////////////////////////////////////////////////

inline PotentialHit Instance::intersect(const Ray &_ray, size_t instID) const
{
  if (!m_group)
    return std::nullopt;

  Ray ray = m_invXfm.has_value() ? xfmRay(m_invXfm.value(), _ray) : _ray;
  auto hit = m_group->intersect(ray, instID);

  if (m_normXfm.has_value() && hit.has_value() && hit->geomID.has_value()) {
    GeometryInfo &gi = getGeometryInfo(hit);
    gi.normal = m_normXfm.value() * vec(gi.normal);
  }

  return hit;
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Instance *, ANARI_INSTANCE);

} // namespace anari
