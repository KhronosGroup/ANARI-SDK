## Copyright 2021-2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

enable_testing()

project(anariUnitTests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  catch_main.cpp

  test_helium_AnariAny.cpp
  test_helium_ParameterizedObject.cpp
  test_helium_RefCounted.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE helium)

add_test(NAME unit_test::helium::AnariAny            COMMAND ${PROJECT_NAME} "[helium_AnariAny]"           )
add_test(NAME unit_test::helium::ParameterizedObject COMMAND ${PROJECT_NAME} "[helium_ParameterizedObject]")
add_test(NAME unit_test::helium::RefCounted          COMMAND ${PROJECT_NAME} "[helium_RefCounted]"         )

## CTS conformance-harness catalog tests ##

add_executable(anariCatalogTests
  catch_main.cpp

  test_cts_catalog.cpp
  test_cts_worldbuilder.cpp
)

target_link_libraries(anariCatalogTests PRIVATE anari_test_scenes)
target_compile_features(anariCatalogTests PRIVATE cxx_std_20)

# Pure harness tests run anywhere; the world-build smoke tests need a device
# (helide) and skip themselves if one cannot be loaded.
add_test(NAME unit_test::cts::catalog COMMAND anariCatalogTests "[cts]~[helide]")
add_test(NAME unit_test::cts::worldbuilder COMMAND anariCatalogTests "[worldbuilder]")
