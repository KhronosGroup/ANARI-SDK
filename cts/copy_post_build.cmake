file (GLOB pyd_files "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BUILD_CONFIG_DIR}/*.pyd")
file (GLOB example_lib "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BUILD_CONFIG_DIR}/anari_library_${REFERENCE_LIB}*")
file (GLOB debug_lib "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${BUILD_CONFIG_DIR}/anari_library_debug*")


file(COPY ${pyd_files} ${example_lib} ${debug_lib} DESTINATION "${CMAKE_CURRENT_LIST_DIR}")
