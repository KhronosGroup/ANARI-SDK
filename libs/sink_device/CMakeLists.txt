## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_sink LANGUAGES CXX)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_custom_target(generate_sink_device
    COMMAND ${Python3_EXECUTABLE} ${ANARI_JSON_PATH}/generate_queries.py
      --json ${ANARI_JSON_PATH}
      --device ${ANARI_JSON_PATH}/devices/generic_device.json
      --prefix SinkDevice
      --namespace sink_device
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${ANARI_JSON_PATH}/devices/generic_device.json
  )

  add_dependencies(generate_all generate_sink_device)
endif()

add_library(${PROJECT_NAME} SHARED
  SinkDevice.cpp
  SinkDeviceQueries.cpp
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
