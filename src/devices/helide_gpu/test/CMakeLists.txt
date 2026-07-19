cmake_minimum_required(VERSION 3.11)
project(helide_gpu_smoke_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(anari REQUIRED)

add_executable(smoke_test smoke_test.cpp)

target_link_libraries(smoke_test PRIVATE anari::anari)

# stb_image_write (header-only, shipped with the ANARI SDK install)
target_include_directories(smoke_test PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
