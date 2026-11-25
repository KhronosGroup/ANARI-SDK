// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Checks the primitive, object and instance IDs getHitInfo() reports for
// synthetic hits against the scene built in test_hitinfo.ispc.

#include "test_hitinfo_ispc.h"
// std
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t NONE = ~0u;

struct Case
{
  const char *name;
  ispc::HitCase hit;
  ispc::HitIds expected;
};

const std::vector<Case> CASES = {
    {"miss", {0, 0, 0, 0, 0}, {NONE, NONE, NONE}},
    {"primitive.id lookup", {1, 1, 0, 0, 2}, {102, 7, 20}},
    {"primID without primitive.id", {1, 1, 0, 1, 5}, {5, 8, 20}},
    {"primID beyond primitive.id", {1, 1, 0, 0, 9}, {9, 7, 20}},
    {"instance id array element 0", {1, 0, 0, 1, 0}, {0, 8, 10}},
    {"instance id array element 2", {1, 0, 2, 0, 3}, {103, 7, 12}},
};

} // namespace

int main()
{
  // Repeat the cases so they span several SIMD gangs with mixed lanes
  std::vector<ispc::HitCase> hits;
  std::vector<const Case *> expected;
  for (int r = 0; r < 7; r++) {
    for (auto &c : CASES) {
      hits.push_back(c.hit);
      expected.push_back(&c);
    }
  }

  std::vector<ispc::HitIds> results(hits.size());
  ispc::test_getHitInfoIds(hits.data(), results.data(), int(hits.size()));

  int failures = 0;
  for (size_t i = 0; i < results.size(); i++) {
    const auto &e = expected[i]->expected;
    const auto &r = results[i];
    if (r.primId != e.primId || r.objId != e.objId || r.instId != e.instId) {
      std::printf(
          "FAIL %s: got (prim %u, obj %u, inst %u), "
          "expected (prim %u, obj %u, inst %u)\n",
          expected[i]->name,
          r.primId,
          r.objId,
          r.instId,
          e.primId,
          e.objId,
          e.instId);
      failures++;
    }
  }

  std::printf("%s: %zu hits, %d failures\n",
      failures ? "FAIL" : "PASS",
      results.size(),
      failures);
  return failures ? 1 : 0;
}
