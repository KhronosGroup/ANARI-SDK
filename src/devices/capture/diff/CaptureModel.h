// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace anari {
namespace capture_diff {
namespace detail {

// The format version this reader understands
constexpr int kSupportedVersion = 1;

// A parameter of an object, or a field of an array, or a line of a list
struct Entry
{
  std::string name;
  std::string type; // the ANARI type of a parameter; empty for array fields
  std::string value; // the rest of the line
  bool isList{false}; // the value is the list in 'items'
  std::vector<std::string> items;
};

struct Block
{
  std::string id; // "camera@0", "device" or "warnings"
  std::string label; // the part of the ID before the '@'
  bool hasSubtype{false};
  std::string subtype; // as written, with its quotes
  std::vector<Entry> entries; // in file order
  bool isArray() const;
  const Entry *find(const std::string &name) const;
};

struct Model
{
  std::map<std::string, Block> blocks; // objects, by ID
  std::string frameId;
  Block device;
  Block warnings;
};

// Reads a capture. Returns false, and says why in 'error', for text that is
// not a capture or has a version this reader does not support.
bool parseCapture(const std::string &text, Model &model, std::string &error);

// How the elements of an ANARI data type are stored: components of a kind
struct Layout
{
  enum Kind
  {
    FLOAT,
    INT,
    UINT,
    BOOL,
    OTHER
  };
  Kind kind{OTHER};
  size_t components{0};
  size_t componentBytes{0};
  size_t elementBytes() const
  {
    return components * componentBytes;
  }
};

// From the name of a type, such as "ANARI_FLOAT32_VEC3"
Layout layoutOf(const std::string &typeName);

// Whether the value of a parameter of this type is the ID of an object
bool isObjectType(const std::string &typeName);

// Splits "(1, 2, 3)" or "1" into "1", "2", "3"
std::vector<std::string> splitTuple(const std::string &text);

// The shortest text that reads back as the same float or double
std::string formatFloat(double value, bool isDouble);

} // namespace detail
} // namespace capture_diff
} // namespace anari
