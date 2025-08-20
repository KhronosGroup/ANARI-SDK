// Copyright 2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Some preprocessor utilities to more easily switch on
// different ANARI data types.

#if _MSVC_TRADITIONAL
// We need standard support from the preprocessor for the below to work. Make
// sure it is enabled. For the sake of portability, this header should only be
// included in VisRTX C++ files and should not leak into public headers, as we
// don't want to enfore the use of the /Zc:preprocessor flag on downstream
// codes.
#error                                                                         \
    "Traditional MSVC preprocessor is not supported and leads to invalid code, make sure to use the /Zc:preprocessor flag to build"
#endif

#define _HDANARI_EMPTY()
#define _HDANARI_DEFER(id) id _HDANARI_EMPTY()
#define _HDANARI_OBSTRUCT(...) __VA_ARGS__ _HDANARI_DEFER(_HDANARI_EMPTY)()

#define _HDANARI_EXPAND1(...) __VA_ARGS__
#define _HDANARI_EXPAND2(...) _HDANARI_EXPAND1(_HDANARI_EXPAND1(__VA_ARGS__))
#define _HDANARI_EXPAND4(...) _HDANARI_EXPAND2(_HDANARI_EXPAND2(__VA_ARGS__))
#define _HDANARI_EXPAND8(...) _HDANARI_EXPAND4(_HDANARI_EXPAND4(__VA_ARGS__))
#define _HDANARI_EXPAND(...) _HDANARI_EXPAND4(__VA_ARGS__)

// Implementation for the innermost level of recursion

// A signle entry switch statement for debugging purposes
#define _HDANARI_FOR_EACH_DATATYPE_IMPL_1(DataType, Call, ...)                 \
  switch (DataType) {                                                          \
  case ANARI_UFIXED8:                                                          \
    Call(__VA_ARGS__, std::uint8_t, 1, false);                                 \
    break;                                                                     \
  }

#define _HDANARI_FOR_EACH_DATATYPE_IMPL(DataType, Call, ...)                   \
  switch (DataType) {                                                          \
  case ANARI_UFIXED8:                                                          \
    Call(__VA_ARGS__, std::uint8_t, 1, false);                                 \
    break;                                                                     \
  case ANARI_UFIXED8_VEC2:                                                     \
    Call(__VA_ARGS__, std::uint8_t, 2, false);                                 \
    break;                                                                     \
  case ANARI_UFIXED8_VEC3:                                                     \
    Call(__VA_ARGS__, std::uint8_t, 3, false);                                 \
    break;                                                                     \
  case ANARI_UFIXED8_VEC4:                                                     \
    Call(__VA_ARGS__, std::uint8_t, 4, false);                                 \
    break;                                                                     \
  case ANARI_FIXED16:                                                          \
    Call(__VA_ARGS__, std::int16_t, 1, false);                                 \
    break;                                                                     \
  case ANARI_FIXED16_VEC2:                                                     \
    Call(__VA_ARGS__, std::int16_t, 2, false);                                 \
    break;                                                                     \
  case ANARI_FIXED16_VEC3:                                                     \
    Call(__VA_ARGS__, std::int16_t, 3, false);                                 \
    break;                                                                     \
  case ANARI_FIXED16_VEC4:                                                     \
    Call(__VA_ARGS__, std::int16_t, 4, false);                                 \
    break;                                                                     \
  case ANARI_UFIXED16:                                                         \
    Call(__VA_ARGS__, std::uint16_t, 1, false);                                \
    break;                                                                     \
  case ANARI_UFIXED16_VEC2:                                                    \
    Call(__VA_ARGS__, std::uint16_t, 2, false);                                \
    break;                                                                     \
  case ANARI_UFIXED16_VEC3:                                                    \
    Call(__VA_ARGS__, std::uint16_t, 3, false);                                \
    break;                                                                     \
  case ANARI_UFIXED16_VEC4:                                                    \
    Call(__VA_ARGS__, std::uint16_t, 4, false);                                \
    break;                                                                     \
  case ANARI_FIXED32:                                                          \
    Call(__VA_ARGS__, std::int32_t, 1, false);                                 \
    break;                                                                     \
  case ANARI_FIXED32_VEC2:                                                     \
    Call(__VA_ARGS__, std::int32_t, 2, false);                                 \
    break;                                                                     \
  case ANARI_FIXED32_VEC3:                                                     \
    Call(__VA_ARGS__, std::int32_t, 3, false);                                 \
    break;                                                                     \
  case ANARI_FIXED32_VEC4:                                                     \
    Call(__VA_ARGS__, std::int32_t, 4, false);                                 \
    break;                                                                     \
  case ANARI_UFIXED32:                                                         \
    Call(__VA_ARGS__, std::uint32_t, 1, false);                                \
    break;                                                                     \
  case ANARI_UFIXED32_VEC2:                                                    \
    Call(__VA_ARGS__, std::uint32_t, 2, false);                                \
    break;                                                                     \
  case ANARI_UFIXED32_VEC3:                                                    \
    Call(__VA_ARGS__, std::uint32_t, 3, false);                                \
    break;                                                                     \
  case ANARI_UFIXED32_VEC4:                                                    \
    Call(__VA_ARGS__, std::uint32_t, 4, false);                                \
    break;                                                                     \
  case ANARI_FLOAT32:                                                          \
    Call(__VA_ARGS__, float, 1, false);                                        \
    break;                                                                     \
  case ANARI_FLOAT32_VEC2:                                                     \
    Call(__VA_ARGS__, float, 2, false);                                        \
    break;                                                                     \
  case ANARI_FLOAT32_VEC3:                                                     \
    Call(__VA_ARGS__, float, 3, false);                                        \
    break;                                                                     \
  case ANARI_FLOAT32_VEC4:                                                     \
    Call(__VA_ARGS__, float, 4, false);                                        \
    break;                                                                     \
  case ANARI_FLOAT64:                                                          \
    Call(__VA_ARGS__, double, 1, false);                                       \
    break;                                                                     \
  case ANARI_FLOAT64_VEC2:                                                     \
    Call(__VA_ARGS__, double, 2, false);                                       \
    break;                                                                     \
  case ANARI_FLOAT64_VEC3:                                                     \
    Call(__VA_ARGS__, double, 3, false);                                       \
    break;                                                                     \
  case ANARI_FLOAT64_VEC4:                                                     \
    Call(__VA_ARGS__, double, 4, false);                                       \
    break;                                                                     \
  case ANARI_UFIXED8_RGBA_SRGB:                                                \
    Call(__VA_ARGS__, std::uint8_t, 4, true);                                  \
    break;                                                                     \
  case ANARI_UFIXED8_RGB_SRGB:                                                 \
    Call(__VA_ARGS__, std::uint8_t, 3, true);                                  \
    break;                                                                     \
  case ANARI_UFIXED8_RA_SRGB:                                                  \
    Call(__VA_ARGS__, std::uint8_t, 2, true);                                  \
    break;                                                                     \
  case ANARI_UFIXED8_R_SRGB:                                                   \
    Call(__VA_ARGS__, std::uint8_t, 1, true);                                  \
    break;                                                                     \
  }

// Note that we don't support float16, so the following are not handled
//      case ANARI_FLOAT16: Call(__VA_ARGS__, _Float16, 1, false); break; \
//      case ANARI_FLOAT16_VEC2: Call(__VA_ARGS__, _Float16, 2, false); break; \
//      case ANARI_FLOAT16_VEC3: Call(__VA_ARGS__, _Float16, 3, false); break; \
//      case ANARI_FLOAT16_VEC4: Call(__VA_ARGS__, _Float16, 4, false); break; \


// Create an indirection to enable recursive expansion. Use the second one to
// debug macro istanciation
#define _HDANARI_FOR_EACH_DATATYPE_INDIRECT() _HDANARI_FOR_EACH_DATATYPE_IMPL
// #define _HDANARI_FOR_EACH_DATATYPE_INDIRECT()
// _HDANARI_FOR_EACH_DATATYPE_IMPL_1

// Use OBSTRUCT and DEFER to prevent immediate expansion and allow recursion
#define _HDANARI_FOR_EACH_DATATYPE(DataType, Call, ...)                        \
  _HDANARI_EXPAND(_HDANARI_OBSTRUCT(_HDANARI_FOR_EACH_DATATYPE_INDIRECT)()(    \
      DataType, Call, __VA_ARGS__))

// Build another one for recursion. No expand here, as it is already present in
// the above
#define _HDANARI_FOR_EACH_DATATYPE_REC(DataType, Call, ...)                    \
  _HDANARI_OBSTRUCT(_HDANARI_FOR_EACH_DATATYPE_INDIRECT)()(                    \
      DataType, Call, __VA_ARGS__)

// Main macro that enables recursive calls with additional expansion
#define HDANARI_FOR_EACH_DATATYPE(DataType, Call, ...)                         \
  _HDANARI_FOR_EACH_DATATYPE(DataType, Call, __VA_ARGS__)
#define HDANARI_FOR_EACH_DATATYPE_REC(DataType, Call, ...)                     \
  _HDANARI_FOR_EACH_DATATYPE_REC(DataType, Call, __VA_ARGS__)
