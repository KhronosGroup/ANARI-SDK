## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if (TARGET mikktspace)
  return()
endif()

add_library(mikktspace STATIC mikktspace.c)
target_include_directories(mikktspace INTERFACE ${CMAKE_CURRENT_LIST_DIR})
