cmake_minimum_required(VERSION 3.1)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(lib_name "ctsBackend")

project("ctsBackend")

include(pybind11.cmake)

file (GLOB src "src/*.cpp")

pybind11_add_module(${lib_name} 
	${src}
)

target_include_directories(${lib_name} PUBLIC "header")
find_package(anari REQUIRED)

target_link_libraries(${lib_name} 
  PUBLIC
  anari::anari
  anari::anari_utilities)


option(CTS_DEV "Copy compiled python module next to python script to make developing easier" ON)
if (CTS_DEV AND NOT BUILD_SHARED_LIBS)
	  add_custom_command(TARGET ${lib_name}
		COMMAND ${CMAKE_COMMAND}
				-DBUILD_CONFIG_DIR=${CMAKE_CFG_INTDIR}
				-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
				-P "${CMAKE_CURRENT_LIST_DIR}/copy_post_build.cmake"
		VERBATIM
		)
endif()