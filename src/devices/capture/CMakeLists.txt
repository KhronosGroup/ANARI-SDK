## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(anari_library_capture LANGUAGES CXX)

find_package(anari REQUIRED)

project_add_library(SHARED)

project_sources(PRIVATE
  CaptureDevice.cpp
  CaptureFormat.cpp
  CaptureFrame.cpp
  CaptureLibrary.cpp
  CaptureObject.cpp
  CaptureWriter.cpp
)

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE capture_device
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/CaptureDefinitions.json
)

project_link_libraries(PRIVATE anari::helium)
project_compile_definitions(PRIVATE "anari_library_capture_EXPORTS")

set_target_properties(${PROJECT_NAME}
PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
)

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
