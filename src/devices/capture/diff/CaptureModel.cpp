// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureModel.h"

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace anari {
namespace capture_diff {
namespace detail {

namespace {

bool startsWith(const std::string &s, const char *prefix)
{
  return s.rfind(prefix, 0) == 0;
}

std::string trim(const std::string &s)
{
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t'))
    ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
    --b;
  return s.substr(a, b - a);
}

std::string fail(size_t line, const std::string &what)
{
  return "line " + std::to_string(line) + ": " + what;
}

} // namespace

bool Block::isArray() const
{
  return startsWith(label, "array");
}

const Entry *Block::find(const std::string &name) const
{
  for (const auto &e : entries) {
    if (e.name == name)
      return &e;
  }
  return nullptr;
}

bool parseCapture(const std::string &text, Model &model, std::string &error)
{
  // Lines
  std::vector<std::string> lines;
  {
    size_t at = 0;
    while (at <= text.size()) {
      auto end = text.find('\n', at);
      if (end == std::string::npos)
        end = text.size();
      lines.push_back(text.substr(at, end - at));
      at = end + 1;
    }
    if (!lines.empty() && lines.back().empty())
      lines.pop_back();
  }

  const std::string magic = "anari-capture ";
  if (lines.empty() || !startsWith(trim(lines[0]), magic.c_str())) {
    error = "not a capture: the first line is not 'anari-capture <version>'";
    return false;
  }
  const std::string versionText = trim(lines[0]).substr(magic.size());
  const int version = std::atoi(versionText.c_str());
  if (version != kSupportedVersion) {
    error = "unsupported capture format version " + versionText
        + " (this reader supports version " + std::to_string(kSupportedVersion)
        + ")";
    return false;
  }

  size_t i = 1;
  while (i < lines.size()) {
    const std::string header = trim(lines[i]);
    const size_t headerLine = i + 1;
    ++i;
    if (header.empty())
      continue;
    if (header.size() < 3 || header.compare(header.size() - 2, 2, " {") != 0) {
      error = fail(headerLine, "expected a block, '<id> {'");
      return false;
    }

    Block block;
    const std::string head = header.substr(0, header.size() - 2);
    const auto space = head.find(' ');
    block.id = head.substr(0, space);
    if (space != std::string::npos) {
      block.hasSubtype = true;
      block.subtype = trim(head.substr(space + 1));
    }
    block.label = block.id.substr(0, block.id.find('@'));

    bool closed = false;
    while (i < lines.size()) {
      const std::string line = trim(lines[i]);
      const size_t lineNumber = i + 1;
      ++i;
      if (line == "}") {
        closed = true;
        break;
      }
      if (line.empty())
        continue;

      Entry entry;
      if (block.id == "warnings") {
        entry.value = line;
        block.entries.push_back(entry);
        continue;
      }

      const auto nameEnd = line.find(' ');
      if (nameEnd == std::string::npos) {
        error = fail(lineNumber, "expected '<name> <value>'");
        return false;
      }
      entry.name = line.substr(0, nameEnd);
      std::string rest = line.substr(nameEnd + 1);
      const bool isParam = !block.isArray();
      if (isParam) {
        const auto typeEnd = rest.find(' ');
        if (typeEnd == std::string::npos) {
          error = fail(lineNumber, "expected '<name> <type> <value>'");
          return false;
        }
        entry.type = rest.substr(0, typeEnd);
        rest = trim(rest.substr(typeEnd + 1));
      }
      entry.value = rest;

      if (rest == "[]") {
        entry.isList = true;
      } else if (rest == "[") {
        entry.isList = true;
        bool listClosed = false;
        while (i < lines.size()) {
          const std::string item = trim(lines[i]);
          ++i;
          if (item == "]") {
            listClosed = true;
            break;
          }
          entry.items.push_back(item);
        }
        if (!listClosed) {
          error = fail(lineNumber, "the list is not closed with ']'");
          return false;
        }
      }
      block.entries.push_back(std::move(entry));
    }
    if (!closed) {
      error = fail(headerLine, "the block '" + block.id + "' is not closed");
      return false;
    }

    if (block.id == "device") {
      model.device = std::move(block);
    } else if (block.id == "warnings") {
      model.warnings = std::move(block);
    } else {
      if (block.label == "frame" && model.frameId.empty())
        model.frameId = block.id;
      if (block.id.find('@') == std::string::npos) {
        error = fail(headerLine, "'" + block.id + "' is not an ID, 'type@n'");
        return false;
      }
      const std::string id = block.id;
      if (!model.blocks.emplace(id, std::move(block)).second) {
        error = fail(headerLine, "the ID '" + id + "' is used twice");
        return false;
      }
    }
  }

  if (model.frameId.empty()) {
    error = "the capture has no frame";
    return false;
  }
  return true;
}

Layout layoutOf(const std::string &name)
{
  Layout layout;
  std::string rest;
  size_t digits = 0;
  auto scalar = [&](const char *prefix, Layout::Kind kind) {
    const size_t n = std::strlen(prefix);
    if (name.compare(0, n, prefix) != 0)
      return false;
    layout.kind = kind;
    while (n + digits < name.size()
        && std::isdigit((unsigned char)name[n + digits]))
      ++digits;
    layout.componentBytes = std::atoi(name.substr(n, digits).c_str()) / 8;
    rest = name.substr(n + digits);
    return digits > 0;
  };

  if (name == "ANARI_BOOL") {
    layout.kind = Layout::BOOL;
    layout.components = 1;
    layout.componentBytes = 1;
    return layout;
  }
  // The longer prefixes first: "ANARI_UINT" and "ANARI_UFIXED" before the
  // ones they contain
  if (!(scalar("ANARI_FLOAT", Layout::FLOAT)
          || scalar("ANARI_UFIXED", Layout::UINT)
          || scalar("ANARI_FIXED", Layout::INT)
          || scalar("ANARI_UINT", Layout::UINT)
          || scalar("ANARI_INT", Layout::INT)))
    return Layout();

  struct Suffix
  {
    const char *text;
    size_t components;
  };
  static const Suffix suffixes[] = {{"", 1},
      {"_VEC2", 2},
      {"_VEC3", 3},
      {"_VEC4", 4},
      {"_BOX1", 2},
      {"_BOX2", 4},
      {"_BOX3", 6},
      {"_BOX4", 8},
      {"_REGION1", 2},
      {"_REGION2", 4},
      {"_REGION3", 6},
      {"_REGION4", 8},
      {"_MAT2", 4},
      {"_MAT3", 9},
      {"_MAT4", 16},
      {"_MAT2x3", 6},
      {"_MAT3x4", 12},
      {"_QUAT_IJKW", 4},
      {"_R_SRGB", 1},
      {"_RA_SRGB", 2},
      {"_RGB_SRGB", 3},
      {"_RGBA_SRGB", 4}};
  for (const auto &s : suffixes) {
    if (rest == s.text) {
      layout.components = s.components;
      return layout;
    }
  }
  return Layout();
}

bool isObjectType(const std::string &t)
{
  static const char *types[] = {"ANARI_OBJECT",
      "ANARI_ARRAY",
      "ANARI_ARRAY1D",
      "ANARI_ARRAY2D",
      "ANARI_ARRAY3D",
      "ANARI_CAMERA",
      "ANARI_FRAME",
      "ANARI_GEOMETRY",
      "ANARI_GROUP",
      "ANARI_INSTANCE",
      "ANARI_LIGHT",
      "ANARI_MATERIAL",
      "ANARI_RENDERER",
      "ANARI_SURFACE",
      "ANARI_SAMPLER",
      "ANARI_SPATIAL_FIELD",
      "ANARI_VOLUME",
      "ANARI_WORLD"};
  return std::any_of(std::begin(types), std::end(types), [&](const char *s) {
    return t == s;
  });
}

std::vector<std::string> splitTuple(const std::string &text)
{
  std::string inner = text;
  if (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')')
    inner = inner.substr(1, inner.size() - 2);
  std::vector<std::string> parts;
  size_t at = 0;
  while (at <= inner.size()) {
    auto comma = inner.find(',', at);
    if (comma == std::string::npos)
      comma = inner.size();
    parts.push_back(trim(inner.substr(at, comma - at)));
    at = comma + 1;
  }
  return parts;
}

std::string formatFloat(double value, bool isDouble)
{
  if (std::isnan(value))
    return "nan";
  if (std::isinf(value))
    return value < 0 ? "-inf" : "inf";
  if (value == 0.0)
    return std::signbit(value) ? "-0" : "0";

  const int maxDigits = isDouble ? 17 : 9;
  char buf[64];
  for (int digits = 1; digits <= maxDigits; ++digits) {
    std::snprintf(buf, sizeof(buf), "%.*e", digits - 1, value);
    const bool same = isDouble ? std::strtod(buf, nullptr) == value
                               : std::strtof(buf, nullptr) == float(value);
    if (same)
      break;
  }

  std::string text = buf;
  const char *point = std::localeconv()->decimal_point;
  const std::string decimalPoint = point && point[0] ? point : ".";
  const bool negative = text[0] == '-';
  const auto ePos = text.find('e');
  std::string digits = text.substr(negative ? 1 : 0, ePos - (negative ? 1 : 0));
  const auto pointPos = digits.find(decimalPoint);
  if (pointPos != std::string::npos)
    digits.erase(pointPos, decimalPoint.size());
  const int exponent = std::atoi(text.c_str() + ePos + 1);

  std::string out = negative ? "-" : "";
  const int n = int(digits.size());
  if (exponent >= -5 && exponent < 16) {
    if (exponent < 0)
      out += "0." + std::string(-exponent - 1, '0') + digits;
    else if (n <= exponent + 1)
      out += digits + std::string(exponent + 1 - n, '0');
    else
      out += digits.substr(0, exponent + 1) + "." + digits.substr(exponent + 1);
  } else {
    out += digits.substr(0, 1);
    if (n > 1)
      out += "." + digits.substr(1);
    char tail[16];
    std::snprintf(tail,
        sizeof(tail),
        "e%c%02d",
        exponent < 0 ? '-' : '+',
        std::abs(exponent));
    out += tail;
  }
  return out;
}

} // namespace detail
} // namespace capture_diff
} // namespace anari
