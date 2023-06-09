# Copyright 2023 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

project(anari_library_remote LANGUAGES CXX)

cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(anari REQUIRED)
find_package(libjpeg-turbo REQUIRED)
find_package(Snappy REQUIRED) # TODO: not really required, shoudl become a cmake option
set(Boost_INCLUDE_DIRS "3rdparty/boost")
#find_package(Boost COMPONENTS filesystem iostreams system REQUIRED)

# =========================================================
# Client device
# =========================================================

add_library(${PROJECT_NAME} SHARED)

target_sources(${PROJECT_NAME} PRIVATE
    async/connection.cpp
    async/connection_manager.cpp
    async/message.cpp
    ArrayInfo.cpp
    Compression.cpp
    Device.cpp
    Frame.cpp
    Logging.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        anari::anari
        anari::anari_library_debug
        helium
        libjpeg-turbo::turbojpeg
        Snappy::snappy
        #Boost::filesystem
        #Boost::iostreams
        #Boost::system
)

# =========================================================
# Server app
# =========================================================

add_executable(anari-remote-server)

target_sources(anari-remote-server PRIVATE
    async/connection.cpp
    async/connection_manager.cpp
    async/message.cpp
    ArrayInfo.cpp
    Compression.cpp
    Logging.cpp
    Server.cpp
)

target_include_directories(anari-remote-server PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(anari-remote-server
    PUBLIC
        anari::anari
        anari::anari_library_debug
        helium
        libjpeg-turbo::turbojpeg
        Snappy::snappy
        #Boost::filesystem
        #Boost::iostreams
        #Boost::system
        ${CMAKE_THREAD_LIBS_INIT}
)

## Installation ##

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
