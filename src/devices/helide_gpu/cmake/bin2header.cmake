# CMake script to convert a binary file into a C/C++ header with a byte array.
# Usage:
#   cmake -DINPUT_FILE=<path> -DOUTPUT_FILE=<path> -DSYM_NAME=<name> -P bin2header.cmake

file(READ "${INPUT_FILE}" content HEX)
string(LENGTH "${content}" hex_length)
math(EXPR num_bytes "${hex_length} / 2")

# Convert each byte pair to a 0xNN literal followed by ", "
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " bytes "${content}")

file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} — do not edit.
#pragma once
#include <cstdint>
static const uint8_t ${SYM_NAME}[] = {
${bytes}
};
static const uint32_t ${SYM_NAME}_len = ${num_bytes};
")
