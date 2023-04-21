## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_sink LANGUAGES CXX)

anari_generate_queries(
  NAME sink
  PREFIX SinkDevice
  CPP_NAMESPACE anari::sink_device
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/generic_device.json
)

add_library(${PROJECT_NAME} SHARED
  SinkDevice.cpp
  SinkDeviceQueries.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC anari helium)

target_compile_definitions(${PROJECT_NAME}
PRIVATE
  "anari_library_sink_EXPORTS"
)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
