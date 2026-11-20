// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Parallel loops on the tasking system that goes with the device's Embree:
// Embree's internal tasking system when Embree is built from source, TBB when
// an installed Embree is used (anari_embree defines ANARI_EMBREE_TBB then).

#ifdef ANARI_EMBREE_TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#else
#include "algorithms/parallel_for.h"
#endif

namespace anari_embree {

// Calls f(i) for each i in [0, n)
template <typename Index, typename Func>
inline void parallel_for(Index n, const Func &f)
{
#ifdef ANARI_EMBREE_TBB
  tbb::parallel_for(Index(0), n, f);
#else
  embree::parallel_for(n, f);
#endif
}

// Calls f(b, e) over subranges [b, e) of [begin, end), each split down to
// about 'grain' indices
template <typename Index, typename Func>
inline void parallel_for(Index begin, Index end, Index grain, const Func &f)
{
#ifdef ANARI_EMBREE_TBB
  tbb::parallel_for(tbb::blocked_range<Index>(begin, end, grain),
      [&](const tbb::blocked_range<Index> &r) { f(r.begin(), r.end()); });
#else
  embree::parallel_for(begin, end, grain, [&](const embree::range<Index> &r) {
    f(r.begin(), r.end());
  });
#endif
}

} // namespace anari_embree
