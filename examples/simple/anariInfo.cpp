// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// anariInfo opens a library and displays queryable information without
// creating any devices.

#include <stdio.h>
#include <string.h>

// anari
#include "anari/frontend/anari_device_introspection.hpp"

const char *helptext =
    "anariInfo -l <library> [-p -t <type> -s <subtype>]\n"
    "   -p: skip parameter listing\n"
    "   -t <type>: only show objects of a type.\n"
    "      example: -t ANARI_RENDERER\n"
    "   -s <subtype>: only show parameters for objects of a subtype.\n"
    "      example: -s triangle\n";

void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  (void)userData;
  (void)device;
  (void)source;
  (void)sourceType;
  (void)code;
  if (severity == ANARI_SEVERITY_FATAL_ERROR) {
    fprintf(stderr, "[FATAL] %s\n", message);
  } else if (severity == ANARI_SEVERITY_ERROR) {
    fprintf(stderr, "[ERROR] %s\n", message);
  } else if (severity == ANARI_SEVERITY_WARNING) {
    fprintf(stderr, "[WARN ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING) {
    fprintf(stderr, "[PERF ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_INFO) {
    fprintf(stderr, "[INFO ] %s\n", message);
  } else if (severity == ANARI_SEVERITY_DEBUG) {
    fprintf(stderr, "[DEBUG] %s\n", message);
  }
}

int main(int argc, const char **argv)
{
  const char *libraryName = nullptr;
  const char *typeFilter = nullptr;
  const char *subtypeFilter = nullptr;
  bool skipParameters = false;
  bool info = false;
  bool extensionsOnly = false;
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "-l", 2) == 0) {
      if (i + 1 < argc) {
        libraryName = argv[++i];
      } else {
        fprintf(stderr, "missing argument for -l\n");
        return 0;
      }
    }
    if (strncmp(argv[i], "-t", 2) == 0) {
      if (i + 1 < argc) {
        typeFilter = argv[++i];
      } else {
        fprintf(stderr, "missing argument for -t\n");
        return 0;
      }
    }
    if (strncmp(argv[i], "-s", 2) == 0) {
      if (i + 1 < argc) {
        subtypeFilter = argv[++i];
      } else {
        fprintf(stderr, "missing argument for -s\n");
        return 0;
      }
    }
    if (strncmp(argv[i], "-p", 2) == 0)
      skipParameters = true;
    if (strncmp(argv[i], "-i", 2) == 0)
      info = true;
    if (strncmp(argv[i], "-f", 2) == 0)
      extensionsOnly = true;
    if (strncmp(argv[i], "-h", 2) == 0) {
      puts(helptext);
      return 0;
    }
  }

  if (!libraryName) {
    fprintf(stderr, "specify a library name with -l\n");
    return 0;
  }

  printf("SDK version: %i.%i.%i\n",
      ANARI_SDK_VERSION_MAJOR,
      ANARI_SDK_VERSION_MINOR,
      ANARI_SDK_VERSION_PATCH);

  ANARILibrary lib = anariLoadLibrary(libraryName, statusFunc, nullptr);
  const char **devices = anariGetDeviceSubtypes(lib);
  if (devices) {
    printf("Devices:\n");
    for (int i = 0; devices[i]; ++i)
      printf("   %s\n", devices[i]);
  }

  for (int i = 0; devices && devices[i]; ++i) {
    printf("Device \"%s\":\n", devices[i]);

    const char **extensions = anariGetDeviceExtensions(lib, devices[i]);
    if (extensions) {
      printf("   Extensions:\n");
      for (int k = 0; extensions[k]; ++k)
        printf("      %s\n", extensions[k]);
    }
    if (extensionsOnly)
      continue;

    ANARIDevice device = anariNewDevice(lib, devices[i]);
    anari::introspection::QueryOptions queryOptions;
    if (typeFilter)
      queryOptions.typeFilter = typeFilter;
    if (subtypeFilter)
      queryOptions.subtypeFilter = subtypeFilter;
    queryOptions.includeParameters = !skipParameters;
    queryOptions.includeParameterInfo = info;

    anari::introspection::FormatOptions formatOptions;
    formatOptions.indent = "   ";
    formatOptions.parameterNameWidth = 25;
    formatOptions.parameterTypeWidth = 25;
    formatOptions.includeSdkVersion = false;
    formatOptions.includeFrameChannels = true;
    formatOptions.quoteStringValues = true;

    const auto report = anari::introspection::formatDeviceInfo(
        anari::introspection::queryDeviceInfo(device, queryOptions),
        formatOptions);
    fputs(report.c_str(), stdout);
    anariRelease(device, device);
  }
  anariUnloadLibrary(lib);
  return 0;
}
