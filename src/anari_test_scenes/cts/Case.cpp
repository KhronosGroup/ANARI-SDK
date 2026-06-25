// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Case.h"

namespace anari {
namespace cts {

static std::string joinKind(
    const std::vector<CaseValue> &values, AxisKind kind)
{
  std::string out;
  for (const auto &cv : values) {
    if (cv.kind != kind)
      continue;
    if (!out.empty())
      out.push_back('_');
    out += anyToString(cv.value);
  }
  return out;
}

std::string Case::groundTruthKey() const
{
  const std::string perm = joinKind(values, AxisKind::Permutation);
  return perm.empty() ? "default" : perm;
}

std::string Case::id() const
{
  const std::string perm = joinKind(values, AxisKind::Permutation);
  const std::string var = joinKind(values, AxisKind::Variant);
  if (perm.empty() && var.empty())
    return "default";
  if (var.empty())
    return perm;
  if (perm.empty())
    return var;
  return perm + "_" + var;
}

} // namespace cts
} // namespace anari
