// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <array>
// anari_cpp
#include "../Traits.h"

namespace anari {

namespace detail_std {
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
} // namespace detail_std

ANARI_TYPEFOR_SPECIALIZATION(detail_std::vec2, ANARI_FLOAT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::vec3, ANARI_FLOAT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::vec4, ANARI_FLOAT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::ivec2, ANARI_INT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::ivec3, ANARI_INT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::ivec4, ANARI_INT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::uvec2, ANARI_UINT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::uvec3, ANARI_UINT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::uvec4, ANARI_UINT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::box2, ANARI_FLOAT32_BOX2);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::box3, ANARI_FLOAT32_BOX3);
ANARI_TYPEFOR_SPECIALIZATION(detail_std::box4, ANARI_FLOAT32_BOX4);

#ifdef ANARI_STD_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(detail_std::vec2);
ANARI_TYPEFOR_DEFINITION(detail_std::vec3);
ANARI_TYPEFOR_DEFINITION(detail_std::vec4);
ANARI_TYPEFOR_DEFINITION(detail_std::ivec2);
ANARI_TYPEFOR_DEFINITION(detail_std::ivec3);
ANARI_TYPEFOR_DEFINITION(detail_std::ivec4);
ANARI_TYPEFOR_DEFINITION(detail_std::uvec2);
ANARI_TYPEFOR_DEFINITION(detail_std::uvec3);
ANARI_TYPEFOR_DEFINITION(detail_std::uvec4);
ANARI_TYPEFOR_DEFINITION(detail_std::box2);
ANARI_TYPEFOR_DEFINITION(detail_std::box3);
ANARI_TYPEFOR_DEFINITION(detail_std::box4);
#endif

} // namespace anari
