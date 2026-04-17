# cmake -DINPUT_FILE=<path> -DOUTPUT_FILE=<path> -DSYM_NAME=<name> -P str2header.cmake
#
# Reads a text file (e.g. MSL shader source) and emits a C header containing
# a null-terminated const char[] array and a length variable.

file(READ "${INPUT_FILE}" content)
string(LENGTH "${content}" content_len)

# Escape backslashes, double-quotes, and newlines for a C string literal.
string(REPLACE "\\" "\\\\" content "${content}")
string(REPLACE "\"" "\\\"" content "${content}")
string(REPLACE "\n" "\\n\"\n\"" content "${content}")

file(WRITE "${OUTPUT_FILE}"
"// Auto-generated from ${INPUT_FILE} — do not edit.
#pragma once
#include <cstdint>
static const char ${SYM_NAME}[] = \"${content}\";
static const uint32_t ${SYM_NAME}_len = ${content_len};
")
