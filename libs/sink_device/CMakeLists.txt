## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 11)

project(anari_library_sink LANGUAGES CXX)

find_package(anari REQUIRED)

anari_generate_queries(
  NAME sink
  PREFIX SinkDevice
  CPP_NAMESPACE sink_device
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/generic_device.json
)

project_add_library(SHARED SinkDevice.cpp SinkDeviceQueries.cpp SinkLibrary.cpp)
project_link_libraries(PRIVATE anari::helium)
project_compile_definitions(PRIVATE "anari_library_sink_EXPORTS")

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
