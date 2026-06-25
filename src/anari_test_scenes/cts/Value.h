// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari_test_scenes_export.h"
// helium
#include "helium/utility/AnariAny.h"
// std
#include <string>

namespace anari {
namespace cts {

// An axis value, or any value the harness carries by name. Reuses ANARI's own
// type-erased value container so it round-trips through BuildContext's backing
// helium::ParameterizedObject without translation.
using Any = helium::AnariAny;

// An invalid Any, meaning "leave this parameter unset so the device default is
// used". Mirrors the old JSON `null` permutation value. Serializes to "none".
inline Any none()
{
  return Any{};
}

// Stable, filesystem-safe string for an axis value, used to build Case ids and
// ground-truth keys. Scalars render without trailing zeros, strings pass
// through (sanitized), an invalid value renders as "none".
ANARI_TEST_SCENES_INTERFACE std::string anyToString(const Any &v);

} // namespace cts
} // namespace anari
