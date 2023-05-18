## Copyright 2022 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## Establish project ##

project(helium LANGUAGES CXX)

## Build library target ##

project_add_library(STATIC)

project_sources(PRIVATE
  BaseArray.cpp
  BaseDevice.cpp
  BaseFrame.cpp
  BaseGlobalDeviceState.cpp
  BaseObject.cpp

  utility/DeferredCommitBuffer.cpp
  utility/ParameterizedObject.cpp
  utility/TimeStamp.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/anari/backend>
)

project_link_libraries(PUBLIC anari)

## Install library + targets ##

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/anari/backend
  FILES_MATCHING
    PATTERN *.h
)
