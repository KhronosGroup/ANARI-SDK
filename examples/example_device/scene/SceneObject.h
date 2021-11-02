// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Array.h"
#include "../util/Span.h"
// bvh
#include "bvh/BVH.h"
#include "bvh/Primitive.h"
#include "bvh/RayTraverser.h"
#include "bvh/SweepSAHBuilder.h"

namespace anari {
namespace example_device {

struct SceneObject : public Object
{
  SceneObject() = default;

  box3 bounds() const override;

  bool getProperty(const std::string &name,
      ANARIDataType type,
      void *ptr,
      ANARIWaitMask m) override;

  virtual PotentialHit intersect(const Ray &ray, size_t prim) const = 0;

 protected:
  void setBounds(const box3 &b);

  box3 m_bounds;
};

template <typename PRIM_T, typename BASE_CLASS_T = SceneObject>
struct IntersectableObject : public BASE_CLASS_T
{
  using prim_t = PRIM_T;
  IntersectableObject() = default;

  PotentialHit intersect(const Ray &ray, size_t prim) const override;

 protected:
  void buildBVH(const PRIM_T *primitives, size_t numPrims);

  BVH m_bvh;
  const PRIM_T *m_primitives{nullptr};
  size_t m_numPrims{0};
};

template <typename OBJECT_T>
struct IntersectablePtr
{
  OBJECT_T *m_obj;

  PotentialHit intersect(const Ray &ray, size_t prim) const;
  box3 bounds() const;
};

// Helper functions ///////////////////////////////////////////////////////////

template <typename Primitive>
std::pair<std::vector<box3>, std::vector<vec3>>
compute_bounding_boxes_and_centers(const Primitive *p, size_t nPrims)
{
  auto prim_bounds = std::vector<box3>(nPrims);
  auto centers = std::vector<vec3>(nPrims);

#pragma omp parallel for
  for (size_t i = 0; i < nPrims; ++i) {
    const auto b = p[i].bounds();
    prim_bounds[i] = b;
    centers[i] = center(b);
  }

  return std::make_pair(std::move(prim_bounds), std::move(centers));
}

inline box3 compute_bounds_union(const box3 *bboxes, size_t count)
{
  auto bbox = box3();

#pragma omp declare reduction(bbox_extend:box3                                 \
                              : omp_out.extend(omp_in))                        \
    initializer(omp_priv = box3())

#pragma omp parallel for reduction(bbox_extend : bbox)
  for (size_t i = 0; i < count; ++i)
    bbox.extend(bboxes[i]);

  return bbox;
}

// Inlined defintions /////////////////////////////////////////////////////////

// SceneObject //

inline box3 SceneObject::bounds() const
{
  return m_bounds;
}

inline void SceneObject::setBounds(const box3 &b)
{
  m_bounds = b;
}

inline bool SceneObject::getProperty(
    const std::string &name, ANARIDataType type, void *ptr, ANARIWaitMask mask)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    std::memcpy(ptr, &m_bounds, sizeof(m_bounds));
    return true;
  }

  return Object::getProperty(name, type, ptr, mask);
}

// IntersectableObject //

template <typename PRIM_T, typename BASE_CLASS_T>
inline void IntersectableObject<PRIM_T, BASE_CLASS_T>::buildBVH(
    const PRIM_T *primitives, size_t numPrims)
{
  m_bvh = BVH();
  if (!primitives || numPrims == 0) {
    SceneObject::setBounds(box3());
    return;
  }

  m_primitives = primitives;
  m_numPrims = numPrims;

  SweepSAHBuilder builder(m_bvh);
  auto [bboxes, centers] =
      compute_bounding_boxes_and_centers(primitives, numPrims);
  auto global_bbox = compute_bounds_union(bboxes.data(), numPrims);
  builder.build(global_bbox, bboxes.data(), centers.data(), numPrims);

  SceneObject::setBounds(global_bbox);
}

template <typename PRIM_T, typename BASE_CLASS_T>
inline PotentialHit IntersectableObject<PRIM_T, BASE_CLASS_T>::intersect(
    const Ray &ray, size_t) const
{
  if (m_bvh.nodes.empty())
    return std::nullopt;

  SingleRayTraverser traverser(m_bvh, m_primitives);

  return traverser.intersect(ray);
}

// IntersectablePtr //

template <typename OBJECT_T>
inline PotentialHit IntersectablePtr<OBJECT_T>::intersect(
    const Ray &ray, size_t prim) const
{
  return m_obj->intersect(ray, prim);
}

template <typename OBJECT_T>
inline box3 IntersectablePtr<OBJECT_T>::bounds() const
{
  return m_obj->bounds();
}

} // namespace example_device
} // namespace anari