// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "wrap_omp.h"

namespace anari {
namespace example_device {

// Helper functions ///////////////////////////////////////////////////////////

template <typename TO, typename FROM>
inline TO safe_cast(FROM from)
{
  static_assert(sizeof(TO) == sizeof(FROM), "safe_cast<> type size mismatch");
  TO to;
  std::memcpy(&to, &from, sizeof(from));
  return to;
}

// RadixSort //////////////////////////////////////////////////////////////////

/// Parallel implementation of the radix sort algorithm.
template <size_t BITS_PER_ITERATION>
class RadixSort
{
 public:
  /// Performs the sort. Must be called from a parallel region.
  template <typename Key, typename Value>
  void sort_in_parallel(Key *BVH_RESTRICT &keys,
      Key *BVH_RESTRICT &keys_copy,
      Value *BVH_RESTRICT &values,
      Value *BVH_RESTRICT &values_copy,
      size_t count,
      size_t bit_count)
  {
    ASSERT_PARALLEL();

    static constexpr size_t bucket_count = 1 << BITS_PER_ITERATION;
    static constexpr Key mask = (Key(1) << BITS_PER_ITERATION) - 1;

    size_t thread_count = OMP_GET_NUM_THREADS();
    size_t thread_id = OMP_GET_THREAD_NUM();

#pragma omp single
    {
      allThreadBuckets.clear();
      allThreadBuckets.resize((thread_count + 1) * bucket_count);
    }

    for (size_t bit = 0; bit < bit_count; bit += BITS_PER_ITERATION) {
      auto localBuckets = &allThreadBuckets[thread_id * bucket_count];
      std::fill(localBuckets, localBuckets + bucket_count, 0);

#pragma omp for schedule(static)
      for (size_t i = 0; i < count; ++i)
        localBuckets[(keys[i] >> bit) & mask]++;

#pragma omp for
      for (size_t i = 0; i < bucket_count; i++) {
        // Do a prefix sum of the elements in one bucket over all threads
        size_t sum = 0;
        for (size_t j = 0; j < thread_count; ++j) {
          size_t old_sum = sum;
          sum += allThreadBuckets[j * bucket_count + i];
          allThreadBuckets[j * bucket_count + i] = old_sum;
        }
        allThreadBuckets[thread_count * bucket_count + i] = sum;
      }

      for (size_t i = 0, sum = 0; i < bucket_count; ++i) {
        size_t old_sum = sum;
        sum += allThreadBuckets[thread_count * bucket_count + i];
        localBuckets[i] += old_sum;
      }

#pragma omp for schedule(static)
      for (size_t i = 0; i < count; ++i) {
        size_t j = localBuckets[(keys[i] >> bit) & mask]++;
        keys_copy[j] = keys[i];
        values_copy[j] = values[i];
      }

#pragma omp single
      {
        std::swap(keys_copy, keys);
        std::swap(values_copy, values);
      }
    }
  }

  static uint32_t make_key(float x)
  {
    auto mask = uint32_t(1) << (sizeof(float) * CHAR_BIT - 1);
    auto y = safe_cast<uint32_t>(x);
    return (y & mask ? (-y) ^ mask : y) ^ mask;
  }

 private:
  std::vector<size_t> allThreadBuckets;
};

} // namespace example_device
} // namespace anari
