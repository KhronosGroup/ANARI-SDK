// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <stack>

#include "../../RadixSort.h"
#include "BVH.h"

namespace anari {
namespace example_device {

using Mark = uint_fast8_t;
using Key = uint32_t;

struct SweepSAHBuildTask;

struct SAHTaskWorkItem
{
  size_t nodeIndex;
  size_t begin;
  size_t end;
  size_t depth;
};

size_t workSize(const SAHTaskWorkItem &wi);

// This is a top-down, full-sweep SAH-based BVH builder, sorted by stable
// partitioning for traversal efficiency
struct SweepSAHBuilder
{
  SweepSAHBuilder(BVH &bvh);

  void build(const box3 &global_bbox,
      const box3 *bboxes,
      const vec3 *centers,
      size_t primitiveCount);

 private:
  // normalized assumed cost of node isect
  constexpr static float traversal_cost{1};

  friend class SweepSAHBuildTask;

  void run(SweepSAHBuildTask &task, const SAHTaskWorkItem &section);

  // Data //

  size_t taskSpawnThreshold{1024};
  size_t maxDepth{64};
  size_t maxLeafSize{16};

  RadixSort<10> sorter;
  BVH &bvh;
};

struct SweepSAHBuildTask
{
  SweepSAHBuildTask(SweepSAHBuilder &builder,
      const box3 *bboxes,
      const vec3 *centers,
      const std::array<size_t *, 3> &references,
      const std::array<float *, 3> &costs,
      Mark *marks);

  std::optional<std::pair<SAHTaskWorkItem, SAHTaskWorkItem>> build(
      const SAHTaskWorkItem &item);

 private:
  std::pair<float, size_t> findSplit(int axis, size_t begin, size_t end) const;

  // Data //

  SweepSAHBuilder &builder;
  const box3 *bboxes{nullptr};
  const vec3 *centers{nullptr};

  std::array<size_t * BVH_RESTRICT, 3> references;
  std::array<float * BVH_RESTRICT, 3> costs;
  Mark *marks{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline size_t workSize(const SAHTaskWorkItem &wi)
{
  return wi.end - wi.begin;
}

// SweepSAHBuilder //

inline SweepSAHBuilder::SweepSAHBuilder(BVH &bvh) : bvh(bvh) {}

inline void SweepSAHBuilder::build(const box3 &global_bbox,
    const box3 *bboxes,
    const vec3 *centers,
    size_t primitiveCount)
{
  bvh.nodes = std::vector<BVH::Node>(2 * primitiveCount + 1);
  bvh.primIndices = std::vector<size_t>(primitiveCount);

  auto reference_data = std::vector<size_t>(primitiveCount * 3);
  auto cost_data = std::vector<float>(primitiveCount * 3);
  auto key_data = std::vector<Key>(primitiveCount * 2);
  auto mark_data = std::vector<Mark>(primitiveCount);

  std::array<float *, 3> costs = {cost_data.data(),
      cost_data.data() + primitiveCount,
      cost_data.data() + 2 * primitiveCount};

  std::array<size_t *, 3> sorted_references;
  size_t *unsorted_references = bvh.primIndices.data();
  Key *sorted_keys = key_data.data();
  Key *unsorted_keys = key_data.data() + primitiveCount;

  bvh.node_count = 1;
  bvh.nodes[0].setBounds(global_bbox);

#pragma omp parallel
  {
    for (int axis = 0; axis < 3; ++axis) {
#pragma omp single
      {
        sorted_references[axis] = unsorted_references;
        unsorted_references = reference_data.data() + axis * primitiveCount;
        // Make sure that one array is the final array of references used by the
        // BVH
        if (axis != 0 && sorted_references[axis] == bvh.primIndices.data())
          std::swap(sorted_references[axis], unsorted_references);
        assert(axis < 2 || sorted_references[0] == bvh.primIndices.data()
            || sorted_references[1] == bvh.primIndices.data());
      }

#pragma omp for
      for (size_t i = 0; i < primitiveCount; ++i) {
        sorted_keys[i] = sorter.make_key(centers[i][axis]);
        sorted_references[axis][i] = i;
      }

      sorter.sort_in_parallel(sorted_keys,
          unsorted_keys,
          sorted_references[axis],
          unsorted_references,
          primitiveCount,
          sizeof(float) * CHAR_BIT);
    }

#pragma omp single
    {
      SweepSAHBuildTask first_task(
          *this, bboxes, centers, sorted_references, costs, mark_data.data());
      run(first_task, {0, 0, primitiveCount, 0});
    }
  }
}

inline void SweepSAHBuilder::run(
    SweepSAHBuildTask &task, const SAHTaskWorkItem &section)
{
  std::stack<SAHTaskWorkItem> stack;
  stack.push(section);
  while (!stack.empty()) {
    auto work_item = stack.top();
    assert(work_item.depth <= maxDepth);
    stack.pop();

    auto more_work = task.build(work_item);
    if (more_work) {
      if (workSize(more_work->first) > workSize(more_work->second))
        std::swap(more_work->first, more_work->second);

      stack.push(more_work->second);
      auto firstItem = more_work->first;
      if (workSize(firstItem) > taskSpawnThreshold) {
        SweepSAHBuildTask new_task(task);
#pragma omp task firstprivate(new_task, firstItem)
        {
          run(new_task, firstItem);
        }
      } else {
        stack.push(firstItem);
      }
    }
  }
}

// SweepSAHBuildTask //

inline SweepSAHBuildTask::SweepSAHBuildTask(SweepSAHBuilder &builder,
    const box3 *bboxes,
    const vec3 *centers,
    const std::array<size_t *, 3> &references,
    const std::array<float *, 3> &costs,
    Mark *marks)
    : builder(builder),
      bboxes(bboxes),
      centers(centers),
      references{references[0], references[1], references[2]},
      costs{costs[0], costs[1], costs[2]},
      marks(marks)
{}

inline std::optional<std::pair<SAHTaskWorkItem, SAHTaskWorkItem>>
SweepSAHBuildTask::build(const SAHTaskWorkItem &item)
{
  auto &bvh = builder.bvh;
  auto &node = bvh.nodes[item.nodeIndex];

  auto make_leaf = [](BVH::Node &node, size_t begin, size_t end) {
    node.firstItem = begin;
    node.primitiveCount = end - begin;
    node.is_leaf = true;
  };

  if (workSize(item) <= 1 || item.depth >= builder.maxDepth) {
    make_leaf(node, item.begin, item.end);
    return std::nullopt;
  }

  std::pair<float, size_t> bestSplits[3];
  [[maybe_unused]] bool should_spawn_tasks =
      workSize(item) > builder.taskSpawnThreshold;

// Sweep primitives to find the best cost
#pragma omp taskloop if (should_spawn_tasks) grainsize(1) default(shared)
  for (int axis = 0; axis < 3; ++axis)
    bestSplits[axis] = findSplit(axis, item.begin, item.end);

  int bestAxis = 0;
  if (bestSplits[0].first > bestSplits[1].first)
    bestAxis = 1;
  if (bestSplits[bestAxis].first > bestSplits[2].first)
    bestAxis = 2;

  auto split_index = bestSplits[bestAxis].second;

  // Make sure the cost of splitting does not exceed the cost of not splitting
  auto maxSplitCost =
      half_area(node.getBounds()) * (workSize(item) - builder.traversal_cost);
  if (bestSplits[bestAxis].first >= maxSplitCost) {
    if (workSize(item) > builder.maxLeafSize) {
      // Fallback strategy: median split on largest axis
      bestAxis = largest_axis(node.getBounds());
      split_index = (item.begin + item.end) / 2;
    } else {
      make_leaf(node, item.begin, item.end);
      return std::nullopt;
    }
  }

  int other_axis[2] = {(bestAxis + 1) % 3, (bestAxis + 2) % 3};

  for (size_t i = item.begin; i < split_index; ++i)
    marks[references[bestAxis][i]] = 1;
  for (size_t i = split_index; i < item.end; ++i)
    marks[references[bestAxis][i]] = 0;
  auto partition_predicate = [&](size_t i) { return marks[i] != 0; };

  auto left_bbox = box3();
  auto right_bbox = box3();

// Partition reference arrays and compute bounding boxes
#pragma omp taskgroup
  {
#pragma omp task if (should_spawn_tasks) default(shared)
    {
      std::stable_partition(references[other_axis[0]] + item.begin,
          references[other_axis[0]] + item.end,
          partition_predicate);
    }
#pragma omp task if (should_spawn_tasks) default(shared)
    {
      std::stable_partition(references[other_axis[1]] + item.begin,
          references[other_axis[1]] + item.end,
          partition_predicate);
    }
#pragma omp task if (should_spawn_tasks) default(shared)
    {
      for (size_t i = item.begin; i < split_index; ++i)
        left_bbox.extend(bboxes[references[bestAxis][i]]);
    }
#pragma omp task if (should_spawn_tasks) default(shared)
    {
      for (size_t i = split_index; i < item.end; ++i)
        right_bbox.extend(bboxes[references[bestAxis][i]]);
    }
  }

  // Allocate space for children
  size_t first_child;
#pragma omp atomic capture
  {
    first_child = bvh.node_count;
    bvh.node_count += 2;
  }

  auto &left = bvh.nodes[first_child + 0];
  auto &right = bvh.nodes[first_child + 1];
  node.firstItem = first_child;
  node.primitiveCount = 0;
  node.is_leaf = false;

  left.setBounds(left_bbox);
  right.setBounds(right_bbox);
  SAHTaskWorkItem firstItem(
      {first_child + 0, item.begin, split_index, item.depth + 1});
  SAHTaskWorkItem second_item(
      {first_child + 1, split_index, item.end, item.depth + 1});
  return std::make_optional(std::make_pair(firstItem, second_item));
}

inline std::pair<float, size_t> SweepSAHBuildTask::findSplit(
    int axis, size_t begin, size_t end) const
{
  auto bbox = box3();
  for (size_t i = end - 1; i > begin; --i) {
    bbox.extend(bboxes[references[axis][i]]);
    costs[axis][i] = half_area(bbox) * (end - i);
  }
  bbox = box3();
  auto best_split =
      std::pair<float, size_t>(std::numeric_limits<float>::max(), end);
  for (size_t i = begin; i < end - 1; ++i) {
    bbox.extend(bboxes[references[axis][i]]);
    auto cost = half_area(bbox) * (i + 1 - begin) + costs[axis][i + 1];
    if (cost < best_split.first)
      best_split = std::make_pair(cost, i + 1);
  }
  return best_split;
}

} // namespace example_device
} // namespace anari
