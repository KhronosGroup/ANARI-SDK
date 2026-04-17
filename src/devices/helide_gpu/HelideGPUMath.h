// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// glm
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>
// anari
#include <anari/anari_cpp/ext/glm.h>
#include <anari/anari_cpp.hpp>

namespace helide_gpu {

using namespace glm;

struct box3
{
  vec3 lower{std::numeric_limits<float>::max()};
  vec3 upper{-std::numeric_limits<float>::max()};

  void extend(const vec3 &p)
  {
    lower = glm::min(lower, p);
    upper = glm::max(upper, p);
  }

  void extend(const box3 &b)
  {
    lower = glm::min(lower, b.lower);
    upper = glm::max(upper, b.upper);
  }
};

// Transform a bounding box by a 4x4 matrix (tests all 8 corners).
inline box3 xfmBox(const mat4 &m, const box3 &b)
{
  box3 result;
  for (int i = 0; i < 8; ++i) {
    vec3 corner((i & 1) ? b.upper.x : b.lower.x,
        (i & 2) ? b.upper.y : b.lower.y,
        (i & 4) ? b.upper.z : b.lower.z);
    vec4 t = m * vec4(corner, 1.f);
    result.extend(vec3(t));
  }
  return result;
}

} // namespace helide_gpu
