## Copyright 2022 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

## Establish project ##

project(helium LANGUAGES CXX)

## Build library target ##

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
  BaseArray.cpp
  BaseDevice.cpp
  BaseFrame.cpp
  BaseGlobalDeviceState.cpp
  BaseObject.cpp

  utility/DeferredCommitBuffer.cpp
  utility/ParameterizedObject.cpp
  utility/TimeStamp.cpp
)

target_include_directories(${PROJECT_NAME}
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/..>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/anari/backend>
)

target_link_libraries(${PROJECT_NAME} PUBLIC anari)

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
