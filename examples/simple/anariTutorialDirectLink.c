// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Note that this version of the tutorial directly links to
// libanari_library_helide, which exports a function to directly
// construct the "helide" device. This lets you avoid the need to load a
// module first (which opens a shared library at runtime).

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
// anari
#include "anari/ext/helide/anariNewHelideDevice.h"

int main(int argc, const char **argv)
{
  // Here we use the direct function which creates the reference device instead
  // of loading it as a runtime library.

  ANARIDevice dev = anariNewHelideDevice(NULL, NULL);

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
