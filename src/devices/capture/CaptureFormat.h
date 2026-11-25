// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari_cpp.hpp>
// std
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace capture_device {

// How the bytes of one element of an ANARI data type are read: a number of
// components of one scalar kind. Fixed-point types read as their raw integers.
enum class ScalarKind
{
  FLOAT,
  INT,
  UINT,
  BOOL,
  OPAQUE // not numeric: pointers, callbacks, anything else
};

struct ElementLayout
{
  ScalarKind kind{ScalarKind::OPAQUE};
  size_t components{0};
  size_t componentBytes{0};
};

ElementLayout elementLayout(ANARIDataType type);

// One scalar read from an element: the value in the widest type for its kind.
struct Scalar
{
  ScalarKind kind{ScalarKind::OPAQUE};
  size_t bytes{0};
  double f{0.0};
  int64_t i{0};
  uint64_t u{0};
};

Scalar readScalar(
    const ElementLayout &layout, const uint8_t *element, size_t c);

// "3", "(0.1, 0.2, 0.3)": one element, or a tuple of components
std::string formatScalar(const Scalar &s);
std::string formatTuple(const std::vector<Scalar> &components);
std::string formatElement(const ElementLayout &layout, const uint8_t *element);

// The shortest decimal text that reads back as the same float or double
std::string formatFloat(double value, size_t bytes);

// Quoted, with \" \\ \n \r \t and \xHH for other control characters
std::string quoteString(const std::string &s);

// "camera", "array1d", ...: the lowercase name of an object type, used in IDs
const char *objectLabel(ANARIDataType type);

// A 64-bit FNV-1a content hash, as 16 lowercase hex digits, of the element
// type, the dimensions and the element bytes
std::string contentHash(ANARIDataType elementType,
    const std::vector<uint64_t> &dimensions,
    const uint8_t *bytes,
    size_t numBytes);

} // namespace capture_device
