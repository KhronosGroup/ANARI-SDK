// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry/Surface.h"
#include "volume/Volume.h"

namespace anari {
namespace example_device {

struct Group : public SceneObject
{
  Group();

  void commit() override;

  PotentialHit intersect(const Ray &ray, size_t) const override;

 private:
  template <typename T>
  box3 buildBVH(BVH &bvh, const Span<T *> &primitives);

  BVH m_surfaceBvh;
  BVH m_volumeBvh;

  IntrusivePtr<ObjectArray> m_surfaceData;
  IntrusivePtr<ObjectArray> m_volumeData;

  Span<Surface *> m_surfaces;
  Span<Volume *> m_volumes;
};

// Inlined defintions /////////////////////////////////////////////////////////

inline PotentialHit Group::intersect(const Ray &ray, size_t instID) const
{
  PotentialHit surfaceHit;
  PotentialHit volumeHit;

  if (!m_surfaceBvh.empty()) {
    using S_PRIM_T = IntersectablePtr<Surface>;
    SingleRayTraverser traverser(m_surfaceBvh, (S_PRIM_T *)m_surfaces.data());
    surfaceHit = traverser.intersect(ray);
  }

  if (!m_volumeBvh.empty() && !ray.skipVolumes) {
    using V_PRIM_T = IntersectablePtr<Volume>;
    SingleRayTraverser traverser(m_volumeBvh, (V_PRIM_T *)m_volumes.data());
    volumeHit = traverser.intersect(ray);
  }

  if (surfaceHit)
    surfaceHit->instID = instID;

  if (volumeHit)
    volumeHit->instID = instID;

  if (surfaceHit && volumeHit) {
    const bool volumeCloser = surfaceHit->t.lower > volumeHit->t.lower;
    if (volumeCloser) {
      volumeHit->t.upper = std::min(volumeHit->t.upper, surfaceHit->t.lower);
      return volumeHit;
    } else
      return surfaceHit;
  } else if (surfaceHit) {
    return surfaceHit;
  } else
    return volumeHit; // NOTE: could be empty to represent nothing hit at all
}

template <typename T>
inline box3 Group::buildBVH(BVH &bvh, const Span<T *> &primitives)
{
  using PRIM_T = IntersectablePtr<T>;

  bvh = BVH();

  if (!primitives)
    return box3();

  SweepSAHBuilder builder(bvh);
  const auto numPrims = primitives.size();
  auto [bboxes, centers] =
      compute_bounding_boxes_and_centers((PRIM_T *)primitives.data(), numPrims);
  auto global_bbox = compute_bounds_union(bboxes.data(), numPrims);
  builder.build(global_bbox, bboxes.data(), centers.data(), numPrims);

  return global_bbox;
}

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Group *, ANARI_GROUP);

} // namespace anari
