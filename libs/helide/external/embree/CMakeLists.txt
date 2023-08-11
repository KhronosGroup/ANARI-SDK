## Copyright 2022 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(local_embree LANGUAGES CXX)

option(EMBREE_GEOMETRY_CURVE       "" ON)
option(EMBREE_GEOMETRY_GRID        "" OFF)
option(EMBREE_GEOMETRY_INSTANCE    "" ON)
option(EMBREE_GEOMETRY_POINT       "" ON)
option(EMBREE_GEOMETRY_QUAD        "" ON)
option(EMBREE_GEOMETRY_SUBDIVISION "" OFF)
option(EMBREE_GEOMETRY_TRIANGLE    "" ON)
option(EMBREE_GEOMETRY_USER        "" OFF)
if (APPLE AND ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
  option(EMBREE_ISA_NEON           "" OFF)
  option(EMBREE_ISA_NEON2X         "" ON)
else()
  option(EMBREE_ISA_AVX            "" OFF)
  option(EMBREE_ISA_AVX2           "" OFF)
  option(EMBREE_ISA_AVX512         "" OFF)
  option(EMBREE_ISA_SSE42          "" ON)
  option(EMBREE_ISA_SSE2           "" OFF)
endif()
option(EMBREE_ISPC_SUPPORT         "" OFF)
option(EMBREE_STATIC_LIB           "" ON)
option(EMBREE_TUTORIALS            "" OFF)
option(EMBREE_TUTORIALS_GLFW       "" OFF)
set(EMBREE_TASKING_SYSTEM "INTERNAL" CACHE STRING "" FORCE)

anari_sdk_fetch_project(
  NAME anari_helide_embree
  URL "https://github.com/embree/embree/archive/refs/tags/v3.13.5.zip"
  MD5 d87ce56557a38e332cb3e0fa5cac8cfd
  ADD_SUBDIR
)

project_add_library(INTERFACE)
project_link_libraries(INTERFACE embree tasking)
project_compile_definitions(INTERFACE TASKING_INTERNAL)
project_include_directories(INTERFACE ${anari_helide_embree_LOCATION}/common)
if (WIN32)
  project_compile_options(
  INTERFACE
    /D__SSE__ /D__SSE2__ /D__SSE3__ /D__SSSE3__ /D__SSE4_1__ /D__SSE4_2__
  )
endif()
