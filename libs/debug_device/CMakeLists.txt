## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_debug LANGUAGES CXX)

anari_generate_queries(
  NAME debug
  PREFIX Extended
  CPP_NAMESPACE anari::debug_queries
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/experimental_device.json
)

project_add_library(SHARED
  DebugDevice.cpp
  DebugObject.cpp
  DebugBasics.cpp
  DebugLibrary.cpp
  ExtendedQueries.cpp
  CodeSerializer.cpp
)

project_include_directories(
PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

project_link_libraries(PUBLIC anari helium)

project_compile_definitions(PRIVATE "anari_library_debug_EXPORTS")

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
