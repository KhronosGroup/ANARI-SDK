// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Expansion.h"

namespace anari {
namespace cts {

std::vector<Case> expand(const TestDef &test)
{
  std::vector<Case> cases;
  const auto &axes = test.axes;

  auto baseCase = [&]() {
    Case c;
    c.category = test.category;
    c.testName = test.name;
    return c;
  };

  // No axes -> a single default Case.
  if (axes.empty()) {
    cases.push_back(baseCase());
    return cases;
  }

  // An axis with no values empties the whole product.
  for (const auto &ax : axes)
    if (ax.values.empty())
      return cases;

  auto makeCase = [&](const std::vector<size_t> &idx) {
    Case c = baseCase();
    c.values.reserve(axes.size());
    for (size_t a = 0; a < axes.size(); ++a) {
      c.values.push_back(
          CaseValue{axes[a].name, axes[a].values[idx[a]], axes[a].kind});
    }
    return c;
  };

  const size_t n = axes.size();

  if (test.simplified) {
    // One-factor-at-a-time: baseline (all first values), then each additional
    // value of each axis with the others held at their first value.
    const std::vector<size_t> base(n, 0);
    cases.push_back(makeCase(base));
    for (size_t a = 0; a < n; ++a) {
      for (size_t v = 1; v < axes[a].values.size(); ++v) {
        std::vector<size_t> idx = base;
        idx[a] = v;
        cases.push_back(makeCase(idx));
      }
    }
    return cases;
  }

  // Full cartesian product, last axis varying fastest (mixed-radix counter).
  std::vector<size_t> idx(n, 0);
  while (true) {
    cases.push_back(makeCase(idx));
    size_t a = n;
    while (a > 0) {
      --a;
      if (++idx[a] < axes[a].values.size())
        break;
      idx[a] = 0;
      if (a == 0)
        return cases;
    }
  }
}

} // namespace cts
} // namespace anari
