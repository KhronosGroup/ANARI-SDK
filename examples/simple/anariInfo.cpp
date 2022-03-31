// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// anariInfo.cpp
//
// anariInfo opens a library and displays queryable information without
// creating any devices.

#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// anari
#include "anari/anari.h"
#include "anari/type_utility.h"

/******************************************************************/
/* errorFunc(): the callback to use when an error is encountered */
void statusFunc(void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
  (void)userData;
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

const ANARIDataType anonymousTypes[] = {
  ANARI_ARRAY1D,
  ANARI_ARRAY2D,
  ANARI_ARRAY3D,
  ANARI_SURFACE,
  ANARI_GROUP,
  ANARI_INSTANCE,
  ANARI_WORLD,
  ANARI_FRAME,
};

const ANARIDataType namedTypes[] = {
  ANARI_CAMERA,
  ANARI_GEOMETRY,
  ANARI_LIGHT,
  ANARI_MATERIAL,
  ANARI_RENDERER,
  ANARI_SAMPLER,
  ANARI_SPATIAL_FIELD,
  ANARI_VOLUME
};

/******************************************************************/
int main(int argc, const char **argv)
{
  const char *libraryName = NULL;
  const char *deviceName = NULL;
  for(int i = 1;i<argc;++i) {
    if(strncmp(argv[i], "-l", 2) == 0 && i+1<argc) {
      libraryName = argv[++i];
    }
    if(strncmp(argv[i], "-h", 2) == 0) {
      printf("anariInfo -l <library>\n");
    }
  }

  if(libraryName == NULL) {
    fprintf(stderr, "specify a library name with -l\n");
    return 0;
  }
  // Use the 'example' library here, this is where the impl(s) come from
  ANARILibrary lib = anariLoadLibrary(libraryName, statusFunc, NULL);

  const char **devices = anariGetDeviceSubtypes(lib);
  if(devices) {
    printf("Devices:\n");
    for(int i = 0;devices[i];++i) {
      printf("   %s\n", devices[i]);
    }
  }

  for(int i = 0;devices[i];++i) {
    printf("Device \"%s\":\n", devices[i]);
    printf("   Subtypes:\n");
    for(int j = 0;j<sizeof(namedTypes)/sizeof(ANARIDataType);++j) {
      const char **types = anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
      // print subtypes of named types
      printf("      %s: ", anari::toString(namedTypes[j]));
      if(types) {
        for(int k = 0;types[k];++k){
          printf("%s ", types[k]);
        }
      }
      printf("\n");
    }
    printf("   Parameters:\n");
    for(int j = 0;j<sizeof(namedTypes)/sizeof(ANARIDataType);++j) {
      const char **types = anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
      // print subtypes of named types
      if(types) {
        for(int k = 0;types[k];++k){
          printf("      %s %s:\n", anari::toString(namedTypes[j]), types[k]);
          const ANARIParameter *params = anariGetObjectParameters(lib, devices[i], types[k], namedTypes[j]);
          if(params) {
            for(int l = 0;params[l].name;++l){
              printf("         %-30s %s\n", params[l].name, anari::toString(params[l].type));
            }
          }
        }
      }
    }

    for(int j = 0;j<sizeof(anonymousTypes)/sizeof(ANARIDataType);++j) {
      printf("      %s:\n", anari::toString(anonymousTypes[j]));
      const ANARIParameter *params = anariGetObjectParameters(lib, devices[i], 0, anonymousTypes[j]);
      if(params) {
        for(int l = 0;params[l].name;++l){
          printf("         %-30s %s\n", params[l].name, anari::toString(params[l].type));
        }
      }
    }
  }

  anariUnloadLibrary(lib);

  return 0;
}
