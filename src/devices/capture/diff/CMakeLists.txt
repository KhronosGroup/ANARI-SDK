## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## The capture diff library: a C++ library that compares two captures, which
## has no dependency on the capture device or on ANARI

project(anari_capture_diff LANGUAGES CXX)

project_add_library(STATIC)

project_sources(PRIVATE
  CaptureDiff.cpp
  CaptureModel.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(${PROJECT_NAME}
PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN *.h
)

## The command-line tool ##

project(anariCaptureDiff LANGUAGES CXX)

project_add_executable(main.cpp)
project_link_libraries(PRIVATE anari_capture_diff)

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
