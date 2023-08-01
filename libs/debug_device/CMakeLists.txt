## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_debug LANGUAGES CXX)

anari_generate_debug_objects(
  NAME debug
  PREFIX GenericDevice
  CPP_NAMESPACE anari::debug_device
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/generic_device.json
)

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
  GenericDeviceDebugFactory.cpp
  ExtendedQueries.cpp
  CodeSerializer.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

project_link_libraries(PUBLIC anari helium)

project_compile_definitions( PRIVATE "anari_library_debug_EXPORTS")

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    NAMELINK_SKIP
  # on Windows put the dlls into bin
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  # ... and the import lib into the devel package
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/anari
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
