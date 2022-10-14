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
  anari::anari_utilities
  anari_test_scenes)


add_custom_target(generate_cts
    COMMAND ${Python3_EXECUTABLE} ${ANARI_CODE_GEN_ROOT}/generate_queries.py
      --json ${ANARI_CODE_GEN_ROOT}
      --prefix cts
      --device ${ANARI_CODE_GEN_ROOT}/devices/extended_device.json
      --namespace cts
      --includefile
	  --out src
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/createReferenceData.py ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/cts.py ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}/createReferenceData.py
	  --test_scenes ${CMAKE_CURRENT_LIST_DIR}/test_scenes
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${ANARI_CODE_GEN_ROOT}/devices/extended_device.json
  )

add_dependencies(generate_cts ${lib_name})


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