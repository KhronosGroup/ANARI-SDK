## Copyright 2023 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

project(anariViewer LANGUAGES C CXX)

if (NOT TARGET anari::anari)
  find_package(anari REQUIRED COMPONENTS viewer)
endif()

project_add_executable(main.cpp ui_layout.cpp)
project_link_libraries(PRIVATE anari::anari_viewer)

if (INSTALL_VIEWER)
  install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
