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

const char *helptext =
"anariInfo -l <library> [-p -t <type> -s <subtype>]\n"
"   -p: skip parameter listing\n"
"   -t <type>: only show parameters for objects of a type.\n"
"      example: -t ANARI_RENDERER\n"
"   -s <subtype>: only show parameters for objects of a subtype.\n"
"      example: -s triangle\n";

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
  const char *typeFilter = NULL;
  const char *subtypeFilter = NULL;
  bool skipParameters = 0;
  for(int i = 1;i<argc;++i) {
    if(strncmp(argv[i], "-l", 2) == 0) {
      if(i+1<argc) {
        libraryName = argv[++i];
      } else {
        fprintf(stderr, "missing argument for -l\n");
        return 0;
      }
    }
    if(strncmp(argv[i], "-t", 2) == 0) {
      if(i+1<argc) {
        typeFilter = argv[++i];
      } else {
        fprintf(stderr, "missing argument for -t\n");
        return 0;
      }
    }
    if(strncmp(argv[i], "-s", 2) == 0) {
      if(i+1<argc) {
        subtypeFilter = argv[++i];
      } else {
        fprintf(stderr, "missing argument for -s\n");
        return 0;
      }
    }
    if(strncmp(argv[i], "-p", 2) == 0) {
      skipParameters = true;
    }
    if(strncmp(argv[i], "-h", 2) == 0) {
      puts(helptext);
      return 0;
    }
  }

  if(libraryName == NULL) {
    fprintf(stderr, "specify a library name with -l\n");
    return 0;
  }

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

    if(!skipParameters) {
      printf("   Parameters:\n");
      for(int j = 0;j<sizeof(namedTypes)/sizeof(ANARIDataType);++j) {
        if(typeFilter && strstr(anari::toString(namedTypes[j]), typeFilter) == nullptr) {
          continue;
        }

        const char **types = anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
        // print subtypes of named types
        if(types) {
          for(int k = 0;types[k];++k){
            if(subtypeFilter && strstr(types[k], subtypeFilter) == nullptr) {
              continue;
            }
            printf("      %s %s:\n", anari::toString(namedTypes[j]), types[k]);
            const ANARIParameter *params = anariGetObjectParameters(lib, devices[i], types[k], namedTypes[j]);
            if(params) {
              for(int l = 0;params[l].name;++l){
                printf("         %-25s %-25s", params[l].name, anari::toString(params[l].type));
                int32_t *required = (int32_t*)anariGetParameterInfo(lib, devices[i], types[k], namedTypes[j],
                  params[l].name, params[l].type, "required", ANARI_BOOL);
                if(required && *required) {
                  printf("required");
                }
                printf("\n");
              }
            }
          }
        }
      }

      if(subtypeFilter == nullptr) {
        for(int j = 0;j<sizeof(anonymousTypes)/sizeof(ANARIDataType);++j) {
          if(typeFilter && strstr(anari::toString(anonymousTypes[j]), typeFilter) == nullptr) {
            continue;
          }

          printf("      %s:\n", anari::toString(anonymousTypes[j]));
          const ANARIParameter *params = anariGetObjectParameters(lib, devices[i], 0, anonymousTypes[j]);
          if(params) {
            for(int l = 0;params[l].name;++l){
              printf("         %-25s %-25s", params[l].name, anari::toString(params[l].type));
              int32_t *required = (int32_t*)anariGetParameterInfo(lib, devices[i], nullptr, anonymousTypes[j],
                params[l].name, params[l].type, "required", ANARI_BOOL);
              if(required && *required) {
                printf("required");
              }
              printf("\n");
            }
          }
        }
      }
    }
  }
  anariUnloadLibrary(lib);

  return 0;
}
