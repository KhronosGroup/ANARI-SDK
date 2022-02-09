## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_debug LANGUAGES CXX)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_custom_target(generate_generic_debug_objects
    COMMAND ${Python3_EXECUTABLE} ${ANARI_JSON_PATH}/generate_debug_objects.py
      --json ${ANARI_JSON_PATH}
      --device ${ANARI_JSON_PATH}/devices/generic_device.json
      --prefix GenericDevice
      --namespace debug_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${ANARI_JSON_PATH}/devices/generic_device.json
  )

  add_dependencies(generate_all generate_generic_debug_objects)
endif()

add_library(${PROJECT_NAME} SHARED
  DebugDevice.cpp
  DebugObject.cpp
  DebugBasics.cpp
  GenericDeviceDebugFactory.cpp
)

target_include_directories(${PROJECT_NAME}
PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
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
