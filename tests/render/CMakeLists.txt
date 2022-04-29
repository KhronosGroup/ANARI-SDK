## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(anariRenderTests LANGUAGES CXX)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE anari_test_scenes stb_image)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set(TEST_SCENES
  "cornell_box"
  "gravity_spheres_volume"
  "instanced_cubes"
  "textured_cube"
  "random_spheres"
)

foreach(SCENE ${TEST_SCENES})
  add_test(NAME render_test::${SCENE} COMMAND ${PROJECT_NAME} --scene ${SCENE})
endforeach()