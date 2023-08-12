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
#include "anari/anari.h"

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
  }
}

int main(int argc, const char **argv)
{
  // Using the 'environment' library name will trigger trying to load the
  // name defined in the ANARI_LIBRARY environment variable.
  ANARILibrary lib = anariLoadLibrary("environment", statusFunc, NULL);

  if (lib)
    printf("SUCCESS: was able to load the library from the environment!\n");
  else
    printf("ERROR: the library in ANARI_LIBRARY failed to load!\n");

  // Use the library like any other ANARI application...
  // ...
  // ...

  // Cleanup + exit

  anariUnloadLibrary(lib);
  return 0;
}
