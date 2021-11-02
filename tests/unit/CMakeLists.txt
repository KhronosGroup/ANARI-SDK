## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

enable_testing()

project(anari_unit_tests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  catch_main.cpp

  test_Any.cpp
  test_Optional.cpp
  test_ParameterizedObject.cpp
  test_RefCounted.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE anari_utilities)

if(NOT WIN32) # Tests which are broken on Windows with unknown fixes (for now)
  add_test(NAME Any                 COMMAND ${PROJECT_NAME} "[Any]")
  add_test(NAME Optional            COMMAND ${PROJECT_NAME} "[Optional]")
  add_test(NAME ParameterizedObject COMMAND ${PROJECT_NAME} "[ParameterizedObject]")
  add_test(NAME RefCounted          COMMAND ${PROJECT_NAME} "[RefCounted]")
endif()
