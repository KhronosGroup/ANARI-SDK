// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ptc
#include "WrappedLibrary.h"
// std
#include <cstdlib>

namespace anari_ptc {

namespace {

constexpr const char *DEFAULT_LIBRARY = "helide";

std::string loadFailure(const std::string &name, const char *source)
{
  return "failed to load the wrapped ANARI library '" + name + "' (" + source
      + ")";
}

} // namespace

WrappedLibrary loadWrappedLibrary(const std::string &fromParameter,
    ANARIStatusCallback statusCB,
    const void *statusCBUserPtr)
{
  WrappedLibrary w;
  auto load = [&](const std::string &name) {
    w.name = name;
    w.library = anariLoadLibrary(name.c_str(), statusCB, statusCBUserPtr);
    return w.library != nullptr;
  };

  if (!fromParameter.empty()) {
    if (!load(fromParameter))
      w.failure =
          loadFailure(fromParameter, "device parameter 'wrappedLibrary'");
    return w;
  }

  const char *fromEnv = getenv("PTC_ANARI_LIBRARY");
  const bool haveEnv = fromEnv && *fromEnv;
  if (haveEnv) {
    if (load(fromEnv))
      return w;
    w.fallbackWarning = loadFailure(fromEnv, "PTC_ANARI_LIBRARY")
        + "; falling back to '" + DEFAULT_LIBRARY + "'";
  }

  if (!load(DEFAULT_LIBRARY)) {
    w.failure = loadFailure(DEFAULT_LIBRARY,
        haveEnv ? "fallback for PTC_ANARI_LIBRARY" : "the default");
  }
  return w;
}

} // namespace anari_ptc
