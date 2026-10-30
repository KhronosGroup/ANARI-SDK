// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "CaptureValidation.h"

#include "anari_library_capture_queries.h"
// std
#include <algorithm>

namespace capture_device {

namespace {

bool accepts(ANARIDataType declared, ANARIDataType actual)
{
  return declared == actual
      || (declared == ANARI_OBJECT && anari::isObject(actual))
      || (declared == ANARI_ARRAY && anari::isArray(actual));
}

} // namespace

std::vector<std::string> validateObject(const std::string &id,
    ANARIDataType type,
    const std::string &subtype,
    const CommittedParams::List &params)
{
  std::vector<std::string> warnings;
  if (type == ANARI_RENDERER)
    return warnings;

  const char *const *subtypes = query_object_types(type);
  const bool hasSubtypes = subtypes && *subtypes;
  if (hasSubtypes) {
    bool known = false;
    for (const char *const *s = subtypes; *s; ++s)
      known = known || subtype == *s;
    if (!known) {
      warnings.push_back(id + ": unknown subtype \"" + subtype + "\"");
      return warnings;
    }
  }

  const auto *declared = (const ANARIParameter *)query_object_info(type,
      hasSubtypes ? subtype.c_str() : nullptr,
      "parameter",
      ANARI_PARAMETER_LIST);
  if (!declared)
    return warnings;

  std::vector<const CommittedParams::List::value_type *> sorted;
  for (const auto &p : params)
    sorted.push_back(&p);
  std::sort(sorted.begin(), sorted.end(), [](auto *a, auto *b) {
    return a->first < b->first;
  });

  for (const auto *param : sorted) {
    const std::string &name = param->first;
    const ANARIDataType actual = param->second.type();

    std::vector<ANARIDataType> expected;
    for (auto *p = declared; p->name; ++p) {
      if (name == p->name)
        expected.push_back(p->type);
    }

    if (expected.empty()) {
      warnings.push_back(id + ": unknown parameter \"" + name + "\"");
    } else if (!std::any_of(expected.begin(), expected.end(), [&](auto t) {
                 return accepts(t, actual);
               })) {
      std::string list;
      for (auto t : expected)
        list += std::string(list.empty() ? "" : " or ") + anari::toString(t);
      warnings.push_back(id + ": parameter \"" + name + "\" has type "
          + anari::toString(actual) + ", expected " + list);
    }
  }
  return warnings;
}

} // namespace capture_device
