## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

enable_testing()

project(anariUnitTests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  catch_main.cpp

  test_Any.cpp
  test_Optional.cpp
  test_ParameterizedObject.cpp
  test_RefCounted.cpp

  test_helium_AnariAny.cpp
  test_helium_ParameterizedObject.cpp
  test_helium_RefCounted.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE anari_utilities helium)

add_test(NAME unit_test::Any                 COMMAND ${PROJECT_NAME} "[Any]")
add_test(NAME unit_test::Optional            COMMAND ${PROJECT_NAME} "[Optional]")
add_test(NAME unit_test::ParameterizedObject COMMAND ${PROJECT_NAME} "[ParameterizedObject]")
add_test(NAME unit_test::RefCounted          COMMAND ${PROJECT_NAME} "[RefCounted]")

add_test(NAME unit_test::helium::AnariAny            COMMAND ${PROJECT_NAME} "[helium_AnariAny]"           )
add_test(NAME unit_test::helium::ParameterizedObject COMMAND ${PROJECT_NAME} "[helium_ParameterizedObject]")
add_test(NAME unit_test::helium::RefCounted          COMMAND ${PROJECT_NAME} "[helium_RefCounted]"         )
