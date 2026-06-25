// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "anari_test_scenes_export.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <string>

namespace anari {
namespace cts {

// Device introspection in the style of the old `anariInfo`: report the object
// subtypes a device advertises and, optionally, each subtype's parameter
// metadata (parameter type, and with `info` the required flag / default /
// minimum / maximum / allowed values / element types / description /
// sourceFeature). This is genuine device introspection (anariGetObjectSubtypes
// / anariGetObjectInfo / anariGetParameterInfo), distinct from the catalog
// metadata that `query-metadata` dumps.
//
// Operates on an already-created device and returns the formatted report as
// text, so it is unit-testable and the CLI stays a thin caller. typeFilter and
// subtypeFilter restrict output to object types / subtypes whose name contains
// the given substring (empty = no restriction); skipParameters prints only the
// subtype listing.
ANARI_TEST_SCENES_INTERFACE std::string queryDeviceInfo(anari::Device device,
    const std::string &typeFilter = "",
    const std::string &subtypeFilter = "",
    bool skipParameters = false,
    bool info = false);

} // namespace cts
} // namespace anari
