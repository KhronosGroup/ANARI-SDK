// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Filter.h"
// std
#include <cctype>

namespace anari {
namespace cts {

static char lower(char c)
{
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool globMatch(const std::string &pattern, const std::string &text)
{
  size_t p = 0, t = 0;
  size_t star = std::string::npos, mark = 0;

  while (t < text.size()) {
    if (p < pattern.size()
        && (pattern[p] == '?' || lower(pattern[p]) == lower(text[t]))) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }

  while (p < pattern.size() && pattern[p] == '*')
    ++p;
  return p == pattern.size();
}

bool matches(const Filter &f, const TestDef &test)
{
  if (f.pattern.empty())
    return true;

  const std::string id = test.id();

  if (f.pattern.find('*') != std::string::npos
      || f.pattern.find('?') != std::string::npos) {
    return globMatch(f.pattern, id);
  }

  // Case-insensitive substring match.
  std::string needle, haystack;
  needle.reserve(f.pattern.size());
  haystack.reserve(id.size());
  for (char c : f.pattern)
    needle.push_back(lower(c));
  for (char c : id)
    haystack.push_back(lower(c));
  return haystack.find(needle) != std::string::npos;
}

} // namespace cts
} // namespace anari
