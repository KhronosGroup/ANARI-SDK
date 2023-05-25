## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_test_scenes LANGUAGES CXX)

project_add_library(
  anari_test_scenes.cpp

  scenes/scene.cpp

  scenes/demo/cornell_box.cpp
  scenes/demo/gravity_spheres_volume.cpp

  scenes/file/obj.cpp

  scenes/test/attributes.cpp
  scenes/test/instanced_cubes.cpp
  scenes/test/pbr_spheres.cpp
  scenes/test/random_cylinders.cpp
  scenes/test/random_spheres.cpp
  scenes/test/textured_cube.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/anari/anari_test_scenes>
)
project_link_libraries(PUBLIC anari helium PRIVATE $<BUILD_INTERFACE:stb_image>)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME}
  EXPORT_MACRO_NAME "ANARI_TEST_SCENES_INTERFACE"
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
FILES
  ${PROJECT_BINARY_DIR}/${PROJECT_NAME}_export.h
  ${CMAKE_CURRENT_LIST_DIR}/anari_test_scenes.h
  ${CMAKE_CURRENT_LIST_DIR}/scenes/scene.h
DESTINATION
  ${CMAKE_INSTALL_INCLUDEDIR}/anari/anari_test_scenes
)
