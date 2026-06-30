// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Axis.h"
#include "Value.h"
// std
#include <string>
#include <vector>

namespace anari {
namespace cts {

// One resolved axis value within a Case, tagged with the axis kind so the Case
// can tell permutation values (which distinguish ground truth) from variant
// values (which share it).
struct CaseValue
{
  std::string axisName;
  Any value;
  AxisKind kind{AxisKind::Permutation};
};

// One concrete combination of axis values produced by expanding a Test. A Case
// is what actually gets rendered and compared.
struct Case
{
  std::string category;
  std::string testName;
  std::vector<CaseValue> values; // in axis-declaration order

  // Leaf name uniquely identifying this Case within its Test (all axis values).
  std::string id() const;

  // Leaf name shared by every variant of this Case (permutation values only).
  // Cases differing only in variant values share this key and thus a single
  // ground-truth image.
  std::string groundTruthKey() const;
};

} // namespace cts
} // namespace anari
