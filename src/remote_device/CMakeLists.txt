# Copyright 2023-2024 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

project(anari_library_remote LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(anari REQUIRED)
find_package(Threads REQUIRED)
# TODO: libjpeg-turbo (with vectorization) should be a strict requirement as
# encoding, decoding, and sending color buffers takes significantly longer without
find_package(libjpeg-turbo)
find_package(Snappy)
set(Boost_INCLUDE_DIRS "3rdparty/boost")
find_package(Boost COMPONENTS filesystem iostreams system REQUIRED)

if (Snappy_FOUND)
  list(APPEND __remote_extra_libs Snappy::snappy)
  list(APPEND __remote_definitions HAVE_SNAPPY=1)
endif()

if (libjpeg-turbo_FOUND)
  list(APPEND __remote_extra_libs libjpeg-turbo::turbojpeg)
  list(APPEND __remote_definitions HAVE_TURBOJPEG=1)
endif()

# =========================================================
# Client device
# =========================================================

project_add_library(SHARED)

project_sources(PRIVATE
  async/connection.cpp
  async/connection_manager.cpp
  async/message.cpp
  ArrayInfo.cpp
  Buffer.cpp
  Compression.cpp
  Device.cpp
  Frame.cpp
  Library.cpp
  Logging.cpp
)

project_compile_definitions(PUBLIC ${__remote_definitions})
project_include_directories(PUBLIC ${Boost_INCLUDE_DIRS})
project_link_libraries(
PUBLIC
  anari::helium
  ${__remote_extra_libs}
  Boost::filesystem
  Boost::iostreams
  Boost::system
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# =========================================================
# Server app
# =========================================================

project(anariRemoteServer LANGUAGES CXX)

project_add_executable()

project_sources(
PRIVATE
  async/connection.cpp
  async/connection_manager.cpp
  async/message.cpp
  ArrayInfo.cpp
  Buffer.cpp
  Compression.cpp
  Logging.cpp
  Server.cpp
)

project_compile_definitions(PRIVATE ${__remote_definitions})
project_include_directories(PRIVATE ${Boost_INCLUDE_DIRS})
project_link_libraries(
PUBLIC
  anari::helium
  ${__remote_extra_libs}
  ${CMAKE_THREAD_LIBS_INIT}
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  EXPORT anari_Exports
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
