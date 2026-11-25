// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// anari
#include <anari/anari.h>
// std
#include <string>

namespace anari_ptc {

// The library ptc wraps is, in order of precedence:
// 1. the device parameter 'wrappedLibrary', if the app committed one;
//    if it fails to load, there is no wrapped library;
// 2. $PTC_ANARI_LIBRARY, if set; if it fails to load, 'helide' instead;
// 3. 'helide'.
// The library-level extension list has no device, so only 2 and 3 apply.
struct WrappedLibrary
{
  ANARILibrary library{nullptr}; // null if loading failed
  std::string name; // the library loaded, or the last one tried
  std::string fallbackWarning; // set if helide stood in for another library
  std::string failure; // set if 'library' is null
};

WrappedLibrary loadWrappedLibrary(const std::string &fromParameter,
    ANARIStatusCallback statusCB,
    const void *statusCBUserPtr);

} // namespace anari_ptc
