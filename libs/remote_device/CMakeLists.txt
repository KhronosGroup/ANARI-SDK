# Copyright 2023 The Khronos Group
# SPDX-License-Identifier: Apache-2.0

project(anari_library_remote LANGUAGES CXX)

cmake_minimum_required(VERSION 3.22)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(anari REQUIRED)
find_package(Threads REQUIRED)
# TODO: libjpeg-turbo (with vectorization) should be a strict requirement as
# encoding, decoding, and sending color buffers takes significantly longer without
find_package(libjpeg-turbo)
find_package(Snappy)
set(Boost_INCLUDE_DIRS "3rdparty/boost")
#find_package(Boost COMPONENTS filesystem iostreams system REQUIRED)

if (Snappy_FOUND)
    set(__remote_extra_libs ${__remote_extra_libs} Snappy::snappy)
    set(__remote_definitions ${__remote_definitions} HAVE_SNAPPY=1)
endif()

if (libjpeg-turbo_FOUND)
    set(__remote_extra_libs ${__remote_extra_libs} libjpeg-turbo::turbojpeg)
    set(__remote_definitions ${__remote_definitions} HAVE_TURBOJPEG=1)
endif()

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

if (__remote_definitions)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ${__remote_definitions})
endif()
target_include_directories(${PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        anari::anari
        anari::anari_library_debug
        helium
        ${__remote_extra_libs}
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

if (__remote_definitions)
    target_compile_definitions(anari-remote-server PUBLIC ${__remote_definitions})
endif()
target_include_directories(anari-remote-server PUBLIC ${Boost_INCLUDE_DIRS})
target_link_libraries(anari-remote-server
    PUBLIC
        anari::anari
        anari::anari_library_debug
        helium
        ${__remote_extra_libs}
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
