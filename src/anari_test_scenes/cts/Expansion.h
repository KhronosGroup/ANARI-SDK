// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "TestDef.h"
#include "anari_test_scenes_export.h"
// std
#include <vector>

namespace anari {
namespace cts {

// Expand a Test into its Cases.
//
// Full mode (default): the cartesian product over all axes, last axis varying
// fastest.
//
// Simplified mode (TestDef::simplified): one-factor-at-a-time. A single
// baseline Case using every axis's first value, then one Case per additional
// value of each axis with all other axes held at their first value. Yields
// 1 + sum(len(axis) - 1) Cases.
//
// A Test with no axes expands to a single default Case. If any axis has no
// values the product is empty and no Cases are produced.
ANARI_TEST_SCENES_INTERFACE std::vector<Case> expand(const TestDef &test);

} // namespace cts
} // namespace anari
