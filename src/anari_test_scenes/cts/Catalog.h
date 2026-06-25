// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Filter.h"
#include "TestDef.h"
#include "anari_test_scenes_export.h"
// std
#include <set>
#include <string>
#include <vector>

namespace anari {
namespace cts {

// True when every required feature of the test is present in deviceFeatures.
// A test with no required features is always supported.
ANARI_TEST_SCENES_INTERFACE bool isSupported(
    const TestDef &test, const std::set<std::string> &deviceFeatures);

// The in-C++ registry of all Tests. `cts list` enumerates it; `cts run`
// expands and executes a filtered slice of it. Distinct from the pre-existing
// scene registry (registerScene) used by example viewers.
class ANARI_TEST_SCENES_INTERFACE Catalog
{
 public:
  // Register a Test. Returns a reference to the stored definition.
  const TestDef &add(TestDef test);

  const std::vector<TestDef> &tests() const;

  // Sorted, de-duplicated category names.
  std::vector<std::string> categories() const;

  // All tests, in registration order.
  std::vector<const TestDef *> list() const;

  // Tests selected by the filter, in registration order.
  std::vector<const TestDef *> filter(const Filter &f) const;

  size_t size() const
  {
    return m_tests.size();
  }

 private:
  std::vector<TestDef> m_tests;
};

// The process-wide catalog that authored tests register into.
ANARI_TEST_SCENES_INTERFACE Catalog &globalCatalog();

} // namespace cts
} // namespace anari
