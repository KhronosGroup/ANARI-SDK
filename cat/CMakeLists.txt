## Copyright 2022-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

add_subdirectory(external)

project(anariCat LANGUAGES C CXX)

if (NOT TARGET anari::anari)
  find_package(anari REQUIRED COMPONENTS viewer anari_test_scenes)
endif()

project_add_executable(
  main.cpp
  ui_layout.cpp
  Application.cpp
  Timer.cpp
  windows/PerformanceMetricsViewer.cpp
  windows/SceneSelector.cpp
)
project_include_directories(
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
)
project_link_libraries(
  PRIVATE
    anari::anari_viewer
    anari::anari_test_scenes
    CLI11::CLI11
    cat_implot
)
project_compile_features(PRIVATE cxx_std_17)

if (USE_KOKKOS)
  project_link_libraries(PRIVATE Kokkos::kokkos)
  project_compile_definitions(PRIVATE -DUSE_KOKKOS)
endif ()

if (INSTALL_CAT)
  install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
