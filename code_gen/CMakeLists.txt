## Copyright 2021-2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")

  option(INSTALL_CODE_GEN_SCRIPTS "Install scripts in code_gen/" ON)

  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  set(API_JSON api/anari_core_1_0.json api/khr_frame_completion_callback.json)

  add_custom_target(generate_type_utility
    COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/frontend/type_utility.h ${API_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${API_JSON} generate_headers.py
  )

  add_custom_target(generate_extension_utility
    COMMAND ${Python3_EXECUTABLE} generate_extension_utility.py
    -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
    -d devices/experimental_device.json
    -o ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/frontend
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS devices/experimental_device.json generate_extension_utility.py
  )

  add_custom_target(generate_anari_h
    COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/anari.h ${API_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${API_JSON} generate_headers.py
  )

  add_custom_target(generate_anari_enums_h
    COMMAND ${Python3_EXECUTABLE} generate_headers.py ${CMAKE_SOURCE_DIR}/libs/anari/include/anari/frontend/anari_enums.h ${API_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${API_JSON} generate_headers.py
  )
  add_custom_target(generate_headers DEPENDS
      generate_type_utility
      generate_extension_utility
      generate_anari_h
      generate_anari_enums_h
  )

  add_dependencies(generate_all generate_headers)

  add_custom_target(python_bindings
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pyanari_build.py
    -d ${CMAKE_CURRENT_SOURCE_DIR}/devices/generic_device.json
    -j ${CMAKE_CURRENT_SOURCE_DIR}/api/
    -I ${CMAKE_SOURCE_DIR}/libs/anari/include/
    -I ${CMAKE_BINARY_DIR}/libs/anari/include/anari/
    -L ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS pyanari_build.py anari ${API_JSON} devices/generic_device.json
  )

  if (INSTALL_CODE_GEN_SCRIPTS)
    install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/anari
      COMPONENT code_gen
      FILES_MATCHING
        PATTERN *.py
        PATTERN *.h
        PATTERN *.cpp
        PATTERN *.json
        PATTERN templates/CMakeLists.txt
        PATTERN ./CMakeLists.txt EXCLUDE
    )
  endif()

endif()