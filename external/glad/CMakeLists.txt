## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_library(glad_gl STATIC src/gl.c)
add_library(glad_gles2 STATIC src/gles2.c)
target_include_directories(glad_gl PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(glad_gles2 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)