// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BVH.h"

namespace anari {
namespace example_device {

// Helper types ///////////////////////////////////////////////////////////////

template <typename T, size_t SIZE = 64>
struct FixedStack
{
  void push(const T &t);
  T pop();

  bool empty() const;

 private:
  T elements[SIZE];
  size_t size{0};
};

struct NodeIntersector
{
  NodeIntersector(const Ray &ray);

  float intersectAxis(int axis, float p, const Ray &ray) const;

  std::pair<float, float> intersect(
      const BVH::Node &node, const Ray &ray) const;

 private:
  ivec3 octant;
  vec3 scaledOrigin;
  vec3 inverseRayDir;
};

// SingleRayTraverser /////////////////////////////////////////////////////////

template <typename Primitive, bool AnyHit = false, size_t StackSize = 64>
struct SingleRayTraverser
{
  SingleRayTraverser(const BVH &bvh, const Primitive *p);

  PotentialHit intersect(const Ray &ray) const;

 private:
  PotentialHit &intersect_leaf(
      const BVH::Node &node, Ray &ray, PotentialHit &best_hit) const;

  PotentialHit traverse(Ray ray) const;

  // Data //

  const Primitive *primitives{nullptr};
  const BVH &bvh;
};

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// FixedStack //

template <typename T, size_t SIZE>
BVH_ALWAYS_INLINE void FixedStack<T, SIZE>::push(const T &t)
{
  assert(size < SIZE);
  elements[size++] = t;
}

template <typename T, size_t SIZE>
BVH_ALWAYS_INLINE T FixedStack<T, SIZE>::pop()
{
  assert(!empty());
  return elements[--size];
}

template <typename T, size_t SIZE>
BVH_ALWAYS_INLINE bool FixedStack<T, SIZE>::empty() const
{
  return size == 0;
}

// NodeIntersector //

BVH_ALWAYS_INLINE NodeIntersector::NodeIntersector(const Ray &ray)
    : octant(ray.dir.x < 0.f, ray.dir.y < 0.f, ray.dir.z < 0.f)
{
  inverseRayDir = 1.f / ray.dir;
  scaledOrigin = -ray.org * inverseRayDir;
}

BVH_ALWAYS_INLINE float NodeIntersector::intersectAxis(
    int axis, float p, const Ray &ray) const
{
  return p * inverseRayDir[axis] + scaledOrigin[axis];
}

BVH_ALWAYS_INLINE std::pair<float, float> NodeIntersector::intersect(
    const BVH::Node &node, const Ray &ray) const
{
  vec3 entry(intersectAxis(0, node.bounds[0 * 2 + octant[0]], ray),
      intersectAxis(1, node.bounds[1 * 2 + octant[1]], ray),
      intersectAxis(2, node.bounds[2 * 2 + octant[2]], ray));
  vec3 exit(intersectAxis(0, node.bounds[0 * 2 + 1 - octant[0]], ray),
      intersectAxis(1, node.bounds[1 * 2 + 1 - octant[1]], ray),
      intersectAxis(2, node.bounds[2 * 2 + 1 - octant[2]], ray));
  return std::make_pair(
      std::max(entry[0], std::max(entry[1], std::max(entry[2], ray.t.lower))),
      std::min(exit[0], std::min(exit[1], std::min(exit[2], ray.t.upper))));
}

// SingleRayTraverser //

template <typename Primitive, bool AnyHit, size_t StackSize>
BVH_ALWAYS_INLINE
SingleRayTraverser<Primitive, AnyHit, StackSize>::SingleRayTraverser(
    const BVH &bvh, const Primitive *p)
    : bvh(bvh), primitives(p)
{}

template <typename Primitive, bool AnyHit, size_t StackSize>
BVH_ALWAYS_INLINE PotentialHit
SingleRayTraverser<Primitive, AnyHit, StackSize>::intersect(
    const Ray &ray) const
{
  return traverse(ray);
}

template <typename Primitive, bool AnyHit, size_t StackSize>
BVH_ALWAYS_INLINE PotentialHit &
SingleRayTraverser<Primitive, AnyHit, StackSize>::intersect_leaf(
    const BVH::Node &node, Ray &ray, PotentialHit &best_hit) const
{
  assert(node.is_leaf);
  assert(primitives != nullptr);

  size_t begin = node.firstItem;
  size_t end = begin + node.primitiveCount;

  for (size_t i = begin; i < end; ++i) {
    auto primID = bvh.primIndices[i];
    if (auto hit = primitives[primID].intersect(ray, primID)) {
      best_hit = hit;
      if constexpr (AnyHit)
        return best_hit;
      else
        ray.t.upper = hit->t.lower;
    }
  }
  return best_hit;
}

template <typename Primitive, bool AnyHit, size_t StackSize>
BVH_ALWAYS_INLINE PotentialHit
SingleRayTraverser<Primitive, AnyHit, StackSize>::traverse(Ray ray) const
{
  auto best_hit = PotentialHit();

  if (BVH_UNLIKELY(bvh.nodes[0].is_leaf))
    return intersect_leaf(bvh.nodes[0], ray, best_hit);

  NodeIntersector intersector(ray);

  // Eager traversal
  FixedStack<const BVH::Node *, StackSize> stack;
  const BVH::Node *node = bvh.nodes.data();

  while (true) {
    uint32_t first = node->firstItem;
    const BVH::Node *leftChild = &bvh.nodes[first + 0];
    const BVH::Node *rightChild = &bvh.nodes[first + 1];
    auto distanceLeft = intersector.intersect(*leftChild, ray);
    auto distanceRight = intersector.intersect(*rightChild, ray);

    if (distanceLeft.first <= distanceLeft.second) {
      if (BVH_UNLIKELY(leftChild->is_leaf)) {
        if (intersect_leaf(*leftChild, ray, best_hit) && AnyHit)
          break;
        leftChild = nullptr;
      }
    } else
      leftChild = nullptr;

    if (distanceRight.first <= distanceRight.second) {
      if (BVH_UNLIKELY(rightChild->is_leaf)) {
        if (intersect_leaf(*rightChild, ray, best_hit) && AnyHit)
          break;
        rightChild = nullptr;
      }
    } else
      rightChild = nullptr;

    if (BVH_LIKELY((leftChild != nullptr) ^ (rightChild != nullptr))) {
      node = leftChild != nullptr ? leftChild : rightChild;
    } else if (BVH_UNLIKELY((leftChild != nullptr) & (rightChild != nullptr))) {
      if (distanceLeft.first > distanceRight.first)
        std::swap(leftChild, rightChild);
      stack.push(rightChild);
      node = leftChild;
    } else {
      if (stack.empty())
        break;
      node = stack.pop();
    }
  }

  return best_hit;
}

} // namespace example_device
} // namespace anari
