// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
// anari_cpp
#include "../Traits.h"

namespace anari {

namespace std_types {
using vec2 = std::array<float, 2>;
using vec3 = std::array<float, 3>;
using vec4 = std::array<float, 4>;
using ivec2 = std::array<int, 2>;
using ivec3 = std::array<int, 3>;
using ivec4 = std::array<int, 4>;
using uvec2 = std::array<unsigned int, 2>;
using uvec3 = std::array<unsigned int, 3>;
using uvec4 = std::array<unsigned int, 4>;
using box2 = std::array<vec2, 2>;
using box3 = std::array<vec3, 2>;
using box4 = std::array<vec4, 2>;
using mat4 = std::array<vec4, 4>;
} // namespace std_types

ANARI_TYPEFOR_SPECIALIZATION(std_types::vec2, ANARI_FLOAT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(std_types::vec3, ANARI_FLOAT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(std_types::vec4, ANARI_FLOAT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(std_types::ivec2, ANARI_INT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(std_types::ivec3, ANARI_INT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(std_types::ivec4, ANARI_INT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(std_types::uvec2, ANARI_UINT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(std_types::uvec3, ANARI_UINT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(std_types::uvec4, ANARI_UINT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(std_types::box2, ANARI_FLOAT32_BOX2);
ANARI_TYPEFOR_SPECIALIZATION(std_types::box3, ANARI_FLOAT32_BOX3);
ANARI_TYPEFOR_SPECIALIZATION(std_types::box4, ANARI_FLOAT32_BOX4);
ANARI_TYPEFOR_SPECIALIZATION(std_types::mat4, ANARI_FLOAT32_MAT4);

#ifdef ANARI_STD_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(std_types::vec2);
ANARI_TYPEFOR_DEFINITION(std_types::vec3);
ANARI_TYPEFOR_DEFINITION(std_types::vec4);
ANARI_TYPEFOR_DEFINITION(std_types::ivec2);
ANARI_TYPEFOR_DEFINITION(std_types::ivec3);
ANARI_TYPEFOR_DEFINITION(std_types::ivec4);
ANARI_TYPEFOR_DEFINITION(std_types::uvec2);
ANARI_TYPEFOR_DEFINITION(std_types::uvec3);
ANARI_TYPEFOR_DEFINITION(std_types::uvec4);
ANARI_TYPEFOR_DEFINITION(std_types::box2);
ANARI_TYPEFOR_DEFINITION(std_types::box3);
ANARI_TYPEFOR_DEFINITION(std_types::box4);
ANARI_TYPEFOR_DEFINITION(std_types::mat4);
#endif

} // namespace anari
