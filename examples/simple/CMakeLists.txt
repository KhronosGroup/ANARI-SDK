## Copyright 2021 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

add_executable(anariTutorial anariTutorial.c)
target_link_libraries(anariTutorial PRIVATE anari stb_image)

add_executable(anariTutorialCpp anariTutorial.cpp)
target_link_libraries(anariTutorialCpp PRIVATE anari stb_image)

add_executable(anariDebugTutorial anariDebugTutorial.c)
target_link_libraries(anariDebugTutorial PRIVATE anari stb_image)

add_executable(anariInfo anariInfo.cpp)
target_link_libraries(anariInfo PRIVATE anari)

