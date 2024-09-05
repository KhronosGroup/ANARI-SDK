## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

message(STATUS "CMake version: ${CMAKE_VERSION}")

## Language setup

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_BUILD_TYPE_INIT Release)

set(CMAKE_INSTALL_MESSAGE LAZY)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

## Establish project

project(anari VERSION 0.10.1 LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

include(GNUInstallDirs)
include(CMakeDependentOption)

enable_testing()

set(CMAKE_INSTALL_RPATH "$ORIGIN:$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

# Built-in CMake options
option(BUILD_SHARED_LIBS "Build shared libraries instead of static" ON)
option(BUILD_TESTING "Build tests for CTest" ON)
# ANARI specific options
option(USE_DRACO "Use draco when loading glTF files" OFF)
option(USE_WEBP "Use webp when loading glTF files" OFF)
option(USE_KTX "Use ktx when loading glTF files" OFF)
option(BUILD_CTS "Build cts toolkit" ON)
option(BUILD_EXAMPLES "Build example applications and example device" ON)
cmake_dependent_option(BUILD_VIEWER
  "Build interactive viewer app (requires GLFW)"
  ON
  "BUILD_EXAMPLES"
  OFF
)
cmake_dependent_option(BUILD_SCENES_LIB
  "Enable anari_test_scenes library"
  ON
  "NOT BUILD_TESTING;NOT BUILD_EXAMPLES;NOT BUILD_CTS"
  ON
)
option(INSTALL_VIEWER_LIBRARY "Install anari::anari_viewer library target" ON)
option(INSTALL_VIEWER "Install anariViewer app" OFF)
mark_as_advanced(INSTALL_VIEWER)
cmake_dependent_option(VIEWER_ENABLE_GLTF
  "Enable glTF support in viewer"
  OFF
  "BUILD_VIEWER"
  OFF
)
cmake_dependent_option(VIEWER_ENABLE_KTX
  "Enable KTX support in viewer"
  OFF
  "BUILD_VIEWER"
  OFF
)

## The generate_all targets collects all offline code generation targets

set(ANARI_CODE_GEN_ROOT ${CMAKE_SOURCE_DIR}/code_gen)
add_custom_target(generate_all)
include(cmake/anari_generate_frontend.cmake)
include(cmake/anari_generate_codegen.cmake)
include(cmake/cmake_project_commands.cmake)
include(cmake/anari_sdk_fetch_project.cmake)

## Add library and executable targets

add_subdirectory(src)
add_subdirectory(external)
add_subdirectory(code_gen)
if (BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if (BUILD_TESTING)
  include(CTest)
  add_subdirectory(tests)
endif()

if (BUILD_CTS)
  add_subdirectory(cts)
endif()

include(cmake/mark_cache_variables_as_advanced.cmake)

## Configure CMake find_package() config files ##

set(ANARI_CMAKE_INSTALL_DESTINATION
  ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${PROJECT_VERSION}
)

install(EXPORT anari_Exports
  DESTINATION ${ANARI_CMAKE_INSTALL_DESTINATION}
  NAMESPACE anari::
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
INSTALL_DESTINATION
  ${ANARI_CMAKE_INSTALL_DESTINATION}
)

write_basic_package_version_file(
    "${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
DESTINATION
  ${ANARI_CMAKE_INSTALL_DESTINATION}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/cmake
  DESTINATION
    ${ANARI_CMAKE_INSTALL_DESTINATION}
  FILES_MATCHING
    PATTERN *.cmake
    PATTERN Findanari.cmake EXCLUDE
)
