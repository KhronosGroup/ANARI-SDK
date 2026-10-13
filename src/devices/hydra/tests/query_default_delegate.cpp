// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Non-GL query test: the `default` subtype must forward to Storm and hdAnari
// (the reverse bridge, HdAnariRendererPlugin) must be excluded from the
// registry (SPEC §11.2). Regression test for the hydra⇄hdAnari probe
// recursion: with ANARI_LIBRARY=hydra, resolving `default` to hdAnari made
// hdAnari load this library back and re-probe `default`, spawning one worker
// (thread + X connection + GL context) per cycle until the X server's client
// limit, then segfaulting. Set the hostile environment up front so a
// regression hangs/crashes here instead of passing silently.

#include <anari/anari.h>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject,
    ANARIDataType, ANARIStatusSeverity severity, ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERROR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

static std::set<std::string> toSet(const char *const *list)
{
  std::set<std::string> s;
  for (const char *const *p = list; p && *p; ++p)
    s.insert(*p);
  return s;
}

int main()
{
  // The environment that closed the recursion loop: hdAnari resolves its
  // "environment" ANARI library through ANARI_LIBRARY.
  setenv("ANARI_LIBRARY", "hydra", 1);

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }

  std::set<std::string> subtypes = toSet(anariGetDeviceSubtypes(lib));
  std::printf("device subtypes: %zu\n", subtypes.size());
  check(subtypes.count("default") == 1, "'default' subtype present");
  check(subtypes.count("storm") == 1, "'storm' subtype present");
  check(subtypes.count("anari") == 0, "hdAnari excluded from subtype list");

  // The call that never returned under the recursion.
  std::set<std::string> defExt = toSet(anariGetDeviceExtensions(lib, "default"));
  check(!defExt.empty(), "'default' extension query returns");

  std::set<std::string> stormExt = toSet(anariGetDeviceExtensions(lib, "storm"));
  check(defExt == stormExt, "'default' extension list matches storm");

  anariUnloadLibrary(lib);

  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
