// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Value.h"
// std
#include <string>
#include <vector>

namespace anari {
namespace cts {

// Every Axis is either a Permutation or a Variant.
//  - Permutation: values produce *different* output, so each value gets its own
//    ground-truth image.
//  - Variant: values must produce *identical* output (e.g. soup vs indexed).
//    All values share one ground-truth image, asserting equivalence.
enum class AxisKind
{
  Permutation,
  Variant,
};

// A named parameter dimension a Test is expanded over.
struct Axis
{
  std::string name;
  std::vector<Any> values;
  AxisKind kind{AxisKind::Permutation};
};

} // namespace cts
} // namespace anari
