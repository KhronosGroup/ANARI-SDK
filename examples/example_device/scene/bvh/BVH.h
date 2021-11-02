// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <climits>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define BVH_RESTRICT __restrict
#define BVH_ALWAYS_INLINE __attribute__((always_inline)) inline
#define BVH_LIKELY(x) __builtin_expect(x, true)
#define BVH_UNLIKELY(x) __builtin_expect(x, false)
#elif defined(_MSC_VER)
#define BVH_RESTRICT __restrict
#define BVH_ALWAYS_INLINE __forceinline inline
#define BVH_LIKELY(x) x
#define BVH_UNLIKELY(x) x
#else
#define BVH_RESTRICT
#define BVH_ALWAYS_INLINE inline
#define BVH_LIKELY(x) x
#define BVH_UNLIKELY(x) x
#endif

namespace anari {
namespace example_device {

/// This structure represents a BVH with a list of nodes and primitives indices.
/// The memory layout is such that the children of a node are always grouped
/// together. This means that each node only needs one index to point to its
/// children, as the other child can be obtained by adding one to the index of
/// the first child. The root of the hierarchy is located at index 0 in the
/// array of nodes.
struct BVH
{
  struct Node
  {
    Node() = default;

    void setBounds(const box3 &bbox);
    box3 getBounds() const;

    // Data //

    float bounds[6];
    bool is_leaf : sizeof(uint8_t);
    uint32_t primitiveCount : sizeof(uint32_t) * CHAR_BIT - 1;
    uint32_t firstItem{0}; // child or prim
  };

  bool empty() const;

  static size_t sibling(size_t index);
  static bool is_left_sibling(size_t index);

  // Data //

  std::vector<Node> nodes;
  std::vector<size_t> primIndices;

  size_t node_count = 0;
};

static_assert(sizeof(BVH::Node) == 32, "BVH::Node is too big!");

// Inlined definitions ////////////////////////////////////////////////////////

BVH_ALWAYS_INLINE void BVH::Node::setBounds(const box3 &bbox)
{
  bounds[0] = bbox.lower[0];
  bounds[1] = bbox.upper[0];
  bounds[2] = bbox.lower[1];
  bounds[3] = bbox.upper[1];
  bounds[4] = bbox.lower[2];
  bounds[5] = bbox.upper[2];
}

BVH_ALWAYS_INLINE box3 BVH::Node::getBounds() const
{
  return box3(vec3(bounds[0], bounds[2], bounds[4]),
      vec3(bounds[1], bounds[3], bounds[5]));
}

BVH_ALWAYS_INLINE bool BVH::empty() const
{
  return nodes.empty();
}

BVH_ALWAYS_INLINE size_t BVH::sibling(size_t index)
{
  assert(index != 0);
  return index & 0x1 ? index + 1 : index - 1;
}

BVH_ALWAYS_INLINE bool BVH::is_left_sibling(size_t index)
{
  assert(index != 0);
  return index & 0x1;
}

} // namespace example_device
} // namespace anari
