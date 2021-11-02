// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <glm/glm.hpp>
#include <glm/ext.hpp>

template <typename T>
struct range_t
{
  range_t() = default;
  range_t(const T &t) : lower(t), upper(t) {}
  range_t(const T &_lower, const T &_upper) : lower(_lower), upper(_upper) {}

  T size() const
  {
    return upper - lower;
  }

  T center() const
  {
    return .5f * (lower + upper);
  }

  void extend(const T &t)
  {
    lower = min(lower, t);
    upper = max(upper, t);
  }

  void extend(const range_t<T> &t)
  {
    lower = min(lower, t.lower);
    upper = max(upper, t.upper);
  }

  T clamp(const T &t) const
  {
    return max(lower, min(t, upper));
  }

  T lower, upper;
};

using glm_box3 = range_t<glm::vec3>;
