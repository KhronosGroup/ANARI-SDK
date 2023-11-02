## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_viewer_glad LANGUAGES C)

project_add_library(INTERFACE)
project_sources(INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/glad.c>)
project_include_directories(
INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
)
project_link_libraries(INTERFACE ${CMAKE_DL_LIBS})
