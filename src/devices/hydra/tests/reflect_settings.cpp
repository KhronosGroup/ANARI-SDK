// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises renderer render-settings reflection (SPEC §11.3): every
// HdRenderSettingDescriptor the bound delegate publishes must be discoverable
// as a typed ANARI renderer parameter under its verbatim Hydra token, alongside
// the core translation-table aliases (§4.7). No frame is rendered — this is a
// pure introspection test through the core ANARI entry point
// anariGetObjectInfo(...,"parameter",ANARI_PARAMETER_LIST).

#include <anari/anari.h>

#include <cstdio>
#include <cstring>
#include <string>

static int g_errors = 0;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN",
        message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

// Return the reflected parameter's type, or ANARI_UNKNOWN if absent.
static ANARIDataType findParam(const ANARIParameter *list, const char *name)
{
  for (; list && list->name; ++list)
    if (std::strcmp(list->name, name) == 0)
      return list->type;
  return ANARI_UNKNOWN;
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "embree";

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, subtype);
  if (!dev)
    return 2;

  const ANARIParameter *params = (const ANARIParameter *)anariGetObjectInfo(
      dev, ANARI_RENDERER, "default", "parameter", ANARI_PARAMETER_LIST);

  check(params != nullptr, "renderer parameter list is non-null");

  int count = 0;
  std::printf("reflected renderer parameters (%s):\n", subtype);
  for (const ANARIParameter *p = params; p && p->name; ++p, ++count)
    std::printf("  - %-32s type=%d\n", p->name, (int)p->type);
  check(count > 0, "renderer parameter list is non-empty");

  // Core translation-table alias must always be present and typed INT32 (§4.7).
  check(findParam(params, "pixelSamples") == ANARI_INT32,
      "core alias 'pixelSamples' present as ANARI_INT32");
  // The bridge-reserved background alias is reflected as FLOAT32_VEC4.
  check(findParam(params, "background") == ANARI_FLOAT32_VEC4,
      "core alias 'background' present as ANARI_FLOAT32_VEC4");

  if (std::string(subtype) == "embree") {
    // HdEmbree publishes enableSceneColors (bool) verbatim (§11.3).
    ANARIDataType t = findParam(params, "enableSceneColors");
    check(t == ANARI_BOOL,
        "embree delegate token 'enableSceneColors' reflected as ANARI_BOOL");
    // convergedSamplesPerPixel is the Hydra target of the pixelSamples alias,
    // so it must NOT appear verbatim (translation-table alias wins collisions).
    check(findParam(params, "convergedSamplesPerPixel") == ANARI_UNKNOWN,
        "aliased token 'convergedSamplesPerPixel' hidden (translation wins)");
    // ambientOcclusionSamples is an int render setting HdEmbree publishes.
    check(findParam(params, "ambientOcclusionSamples") == ANARI_INT32,
        "embree token 'ambientOcclusionSamples' reflected as ANARI_INT32");

    // getParameterInfo: default value + description for a reflected setting.
    const int8_t *dflt = (const int8_t *)anariGetParameterInfo(dev,
        ANARI_RENDERER,
        "default",
        "enableSceneColors",
        ANARI_BOOL,
        "default",
        ANARI_BOOL);
    check(dflt != nullptr, "enableSceneColors reports a default value");
    const char *desc = (const char *)anariGetParameterInfo(dev,
        ANARI_RENDERER,
        "default",
        "enableSceneColors",
        ANARI_BOOL,
        "description",
        ANARI_STRING);
    check(desc != nullptr && desc[0] != '\0',
        "enableSceneColors reports a non-empty description");
  } else {
    // Storm publishes render settings too; require at least one verbatim token
    // beyond the two core aliases.
    check(count > 2, "storm reflects at least one delegate-native setting");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
