## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## guard: runs another ANARI device in a host process ##
#
# Builds as part of the SDK (BUILD_GUARD_DEVICE), or on its own against an
# installed SDK found with find_package(anari).

cmake_minimum_required(VERSION 3.11)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
  set(STANDALONE_SOURCE_TREE TRUE)
else()
  set(STANDALONE_SOURCE_TREE FALSE)
endif()

## Language setup ##

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE_INIT Release)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

## Establish project ##

project(anari_library_guard LANGUAGES CXX)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if (STANDALONE_SOURCE_TREE)
    message(FATAL_ERROR "The guard device only supports Linux.")
  else()
    message(STATUS "The guard device only supports Linux: not built.")
    return()
  endif()
endif()
set(GUARD_PLATFORM_DIR common/platform/linux)

# The adapter starts the host with pidfd_spawn(), which glibc has from 2.39.
include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(pidfd_spawn "spawn.h" GUARD_HAVE_PIDFD_SPAWN)
unset(CMAKE_REQUIRED_DEFINITIONS)
if (NOT GUARD_HAVE_PIDFD_SPAWN)
  message(FATAL_ERROR
    "The guard device needs pidfd_spawn(), which glibc has from 2.39.")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(GNUInstallDirs)

find_package(anari REQUIRED)
find_package(Threads REQUIRED)

option(GUARD_SANITIZE "Build the guard device with ASan and UBSan" OFF)
if (GUARD_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Not part of any test run that gates: build with this, then run ctest by
# hand. The tests are then run with the suppressions in tests/tsan.supp.
option(GUARD_SANITIZE_THREAD "Build the guard device with ThreadSanitizer" OFF)
if (GUARD_SANITIZE_THREAD)
  if (GUARD_SANITIZE)
    message(FATAL_ERROR
      "GUARD_SANITIZE and GUARD_SANITIZE_THREAD cannot be combined.")
  endif()
  add_compile_options(-fsanitize=thread -g -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
  add_compile_definitions(GUARD_SANITIZE_THREAD)
endif()

## Code shared by the adapter and the host ##

add_library(anari_guard_common STATIC
  ${GUARD_PLATFORM_DIR}/Platform.cpp
)
target_include_directories(anari_guard_common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(anari_guard_common
  PUBLIC anari::anari_headers Threads::Threads ${CMAKE_DL_LIBS})

## The adapter: the ANARI library the application loads ##

add_library(${PROJECT_NAME} MODULE
  adapter/GuardDevice.cpp
  adapter/GuardLibrary.cpp
  adapter/HostConnection.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE anari_guard_common anari::helium)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "GUARD_EXPORT")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(${PROJECT_NAME}
PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
)

## The host ##

# Everything but main() is a library, so a test host can be linked from it.
add_library(anari_guard_host_core STATIC host/Host.cpp)
target_link_libraries(anari_guard_host_core
  PUBLIC anari_guard_common anari::anari
  PRIVATE anari::helium)

add_executable(anariGuardHost host/main.cpp)
target_link_libraries(anariGuardHost PRIVATE anari_guard_host_core)
# Installed, the host is in bin/ and the libraries it loads are in lib/.
set_target_properties(anariGuardHost PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")

## Installation ##

install(TARGETS ${PROJECT_NAME} anariGuardHost
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

## The overhead bench ##

option(GUARD_BUILD_BENCH "Build guardBench, which measures the guard device" ON)
if (GUARD_BUILD_BENCH AND TARGET anari::anari_test_scenes)
  add_subdirectory(bench)
endif()

## Tests ##

if (STANDALONE_SOURCE_TREE)
  option(GUARD_BUILD_TESTING "Build the guard device tests" ON)
  set(GUARD_TESTS ${GUARD_BUILD_TESTING})
  if (GUARD_TESTS)
    enable_testing()
  endif()
else()
  set(GUARD_TESTS ${BUILD_TESTING})
  # Every test wraps helide, and the acceptance run is anariRenderTests.
  if (GUARD_TESTS AND NOT BUILD_HELIDE_DEVICE)
    message(STATUS "The guard device's tests need helide: not built.")
    set(GUARD_TESTS OFF)
  endif()
endif()

if (GUARD_TESTS)
  add_subdirectory(tests)
endif()
