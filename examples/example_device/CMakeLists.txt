## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 17)

project(anari_library_example LANGUAGES CXX)

find_package(OpenMP)

if(EXISTS ${CMAKE_SOURCE_DIR}/.git AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE "REFERENCE_DEVICE_VERSION"
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  set(REFERENCE_DEVICE_VERSION -1)
endif()

add_library(${PROJECT_NAME} SHARED
  glm_extras.cpp

  Array.cpp
  Object.cpp
  ExampleDevice.cpp

  camera/Camera.cpp
  camera/Perspective.cpp

  frame/Frame.cpp

  material/Material.cpp
  material/OBJ.cpp

  renderer/AmbientOcclusion.cpp
  renderer/Debug.cpp
  renderer/Raycast.cpp
  renderer/RayDir.cpp
  renderer/Renderer.cpp

  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp

  scene/geometry/Geometry.cpp
  scene/geometry/Spheres.cpp
  scene/geometry/Surface.cpp
  scene/geometry/Triangles.cpp

  scene/volume/StructuredRegularField.cpp
  scene/volume/SpatialField.cpp
  scene/volume/Volume.cpp
)

target_include_directories(${PROJECT_NAME}
INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
  anari
  anari_utilities
PRIVATE
  glm
  $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
)

target_compile_definitions(${PROJECT_NAME}
PRIVATE
  -DDEVICE_VERSION=${REFERENCE_DEVICE_VERSION}
)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
