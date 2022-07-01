## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_debug LANGUAGES CXX)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_custom_target(generate_generic_debug_objects
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_debug_objects.py
      --json ${ANARI_CODE_GEN_ROOT}
      --device ${ANARI_CODE_GEN_ROOT}/devices/generic_device.json
      --prefix GenericDevice
      --namespace anari::debug_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${ANARI_CODE_GEN_ROOT}/devices/generic_device.json
  )

  add_custom_target(generate_debug_queries
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      --prefix Extended
      --device ${ANARI_CODE_GEN_ROOT}/devices/extended_device.json
      --namespace anari::debug_queries
      --includefile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${ANARI_CODE_GEN_ROOT}/devices/extended_device.json
  )

  add_dependencies(generate_all generate_generic_debug_objects generate_debug_queries)
endif()

add_library(${PROJECT_NAME} SHARED
  DebugDevice.cpp
  DebugObject.cpp
  DebugBasics.cpp
  GenericDeviceDebugFactory.cpp
  ExtendedQueries.cpp
  CodeSerializer.cpp
)

target_include_directories(${PROJECT_NAME}
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
  anari
  anari_utilities
)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

target_compile_definitions(${PROJECT_NAME}
PRIVATE
  "anari_library_debug_EXPORTS"
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
