cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(lib_name "cts")

project("cts")

include(pybind11.cmake)

pybind11_add_module(${lib_name} 
	src/main.cpp
)

target_include_directories(${lib_name} PUBLIC "header")
find_package(anari REQUIRED)

target_link_libraries(${lib_name} 
  PUBLIC
  anari::anari
  anari::anari_utilities)
