// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises geometry parameter-list introspection (SPEC §4.1) for the
// cone/cylinder tube shapes: the query must match the parameters
// Cone.cpp/Cylinder.cpp actually read (SPEC KHR_GEOMETRY_CONE,
// KHR_GEOMETRY_CYLINDER), not a stale or aspirational list. Pure
// introspection through the core ANARI entry point
// anariGetObjectInfo(...,"parameter",ANARI_PARAMETER_LIST); no frame is
// rendered.

#include <anari/anari.h>

#include <cstdio>
#include <cstring>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

static bool hasParam(const ANARIParameter *list, const char *name)
{
  for (; list && list->name; ++list)
    if (std::strcmp(list->name, name) == 0)
      return true;
  return false;
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

  const ANARIParameter *cylinder = (const ANARIParameter *)anariGetObjectInfo(
      dev, ANARI_GEOMETRY, "cylinder", "parameter", ANARI_PARAMETER_LIST);
  check(cylinder != nullptr, "cylinder parameter list is non-null");

  // Cylinder.cpp reads primitive.radius, not vertex.radius (SPEC
  // KHR_GEOMETRY_CYLINDER): the query must advertise the parameter the
  // device actually consumes.
  check(hasParam(cylinder, "primitive.radius"),
      "cylinder parameter list advertises 'primitive.radius'");
  check(!hasParam(cylinder, "vertex.radius"),
      "cylinder parameter list omits 'vertex.radius' (not read by Cylinder.cpp)");
  check(hasParam(cylinder, "vertex.cap"),
      "cylinder parameter list advertises 'vertex.cap'");
  check(hasParam(cylinder, "caps"),
      "cylinder parameter list advertises 'caps'");

  const ANARIParameter *cone = (const ANARIParameter *)anariGetObjectInfo(
      dev, ANARI_GEOMETRY, "cone", "parameter", ANARI_PARAMETER_LIST);
  check(cone != nullptr, "cone parameter list is non-null");

  // Cone.cpp reads vertex.radius (base->apex taper) and does not read
  // primitive.radius; advertising primitive.radius on cone would be wrong.
  check(hasParam(cone, "vertex.radius"),
      "cone parameter list advertises 'vertex.radius'");
  check(!hasParam(cone, "primitive.radius"),
      "cone parameter list omits 'primitive.radius' (not read by Cone.cpp)");
  check(hasParam(cone, "vertex.cap"),
      "cone parameter list advertises 'vertex.cap'");
  check(hasParam(cone, "caps"),
      "cone parameter list advertises 'caps'");

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
