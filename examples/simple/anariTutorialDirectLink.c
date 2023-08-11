// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Note that this version of the tutorial directly links to
// libanari_library_example, which exports a function to directly
// construct the "example" devic. This lets you avoid the need to load a
// module first (which opens a shared library at runtime). Thus the only
// difference between this and 'anariTutorial.c' is how the device is created.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
// anari
#include "anari/ext/helide/anariNewHelideDevice.h"

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
    fprintf(stderr, "[INFO] %s\n", message);
  }
}

int main(int argc, const char **argv)
{
  // Here we use the direct function which creates the reference device instead
  // of loading it as a runtime library.

  ANARIDevice dev = anariNewHelideDevice(statusFunc, NULL);

  if (dev)
    printf("SUCCESS: created the 'helide' device without loading a library!\n");
  else
    printf("ERROR: failed to create the 'helide' device!\n");

  // Use the device like any other ANARI application...
  // ...
  // ...

  // Cleanup + exit

  anariRelease(dev, dev);
  return 0;
}
