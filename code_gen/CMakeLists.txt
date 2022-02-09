## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")

  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  set(API_JSON api/anari_core_1_0.json api/frame_continuation.json)

  add_custom_target(generate_type_utility
    COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/type_utility.h ${API_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${API_JSON} generate_headers.py
  )

  add_custom_target(generate_anari_h
    COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/anari.h ${API_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${API_JSON} generate_headers.py
  )

  add_custom_target(generate_anari_enums_h
    COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/anari_enums.h ${API_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${API_JSON} generate_headers.py
  )

  add_custom_target(generate_headers DEPENDS
      generate_type_utility
      generate_anari_h
      generate_anari_enums_h
  )

  add_dependencies(generate_all generate_headers)

endif()