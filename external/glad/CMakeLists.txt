## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_library(glad STATIC glad.c)
target_include_directories(glad PUBLIC ${CMAKE_CURRENT_LIST_DIR})