## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anari_test_scenes LANGUAGES CXX)

add_library(${PROJECT_NAME} STATIC
  anari_test_scenes.cpp

  scenes/cornell_box.cpp
  scenes/file_obj.cpp
  scenes/gravity_spheres_volume.cpp
  scenes/instanced_cubes.cpp
  scenes/random_spheres.cpp
  scenes/scene.cpp
  scenes/textured_cube.cpp
  scenes/attributes.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(${PROJECT_NAME}
PUBLIC
  anari
  anari_utilities
  glm::glm
  stb_image
)
