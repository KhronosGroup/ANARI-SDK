// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <iostream>

#define PRINT(var) std::cout << #var << "=" << var << std::endl;
#ifdef __WIN32__
#define PING                                                                   \
  std::cout << __FILE__ << "::" << __LINE__ << ": " << __FUNCTION__            \
            << std::endl;
#else
#define PING                                                                   \
  std::cout << __FILE__ << "::" << __LINE__ << ": " << __PRETTY_FUNCTION__     \
            << std::endl;
#endif

// anari
#include <anari/anari_cpp.hpp>
// pxr
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/pxr.h>

namespace anari {

ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfVec2f, ANARI_FLOAT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfVec3f, ANARI_FLOAT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfVec4f, ANARI_FLOAT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfVec2i, ANARI_INT32_VEC2);
ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfVec3i, ANARI_INT32_VEC3);
ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfVec4i, ANARI_INT32_VEC4);
ANARI_TYPEFOR_SPECIALIZATION(PXR_NS::GfMatrix4f, ANARI_FLOAT32_MAT4);

#ifdef HDANARI_TYPE_DEFINITIONS
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfVec2f);
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfVec3f);
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfVec4f);
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfVec2i);
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfVec3i);
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfVec4i);
ANARI_TYPEFOR_DEFINITION(PXR_NS::GfMatrix4f);
#endif

} // namespace anari
