// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../../material/Material.h"
#include "Geometry.h"

namespace anari {
namespace example_device {

struct Surface : public SceneObject
{
  Surface();

  void commit() override;

  const Geometry *geometry() const;

  PotentialHit intersect(const Ray &ray, size_t) const override;

 private:
  IntrusivePtr<Geometry> m_geometry;
  IntrusivePtr<Material> m_material;
};

// Inlined defintions /////////////////////////////////////////////////////////

inline const Geometry *Surface::geometry() const
{
  return m_geometry.ptr;
}

inline PotentialHit Surface::intersect(const Ray &ray, size_t geomID) const
{
  const void *oldModel = ray.currentSurface;
  ray.currentSurface = this;

  auto hit = m_geometry->intersect(ray, geomID);

  if (hitGeometry(hit)) {
    GeometryInfo &gi = getGeometryInfo(hit);
    gi.material = m_material.ptr;
    m_geometry->populateHitColor(gi, hit->primID.value());
    hit->geomID = geomID;
  } else
    ray.currentSurface = oldModel;

  return hit;
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Surface *, ANARI_SURFACE);

} // namespace anari
