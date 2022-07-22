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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <type_traits>

// anari
#include "anari/anari.h"
#include "anari/anari_cpp.hpp"
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

const ANARIDataType anonymousTypes[] = {
  ANARI_DEVICE,
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


template<int T, class Enable = void>
struct param_printer {
  void operator()(const void*) {
  }
};

template<>
struct param_printer<ANARI_STRING, void> {
  void operator()(const void *mem) {
    const char *str = (const char*)mem;
    printf("\"%s\"", str);
  }
};

template<int T>
struct param_printer<T,
  typename std::enable_if<
    std::is_floating_point<
      typename anari::ANARITypeProperties<T>::base_type
    >::value
  >::type> {
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem) {
    const base_type *data = (base_type*)mem;
    printf("%.1f", data[0]);
    for(int i = 1;i<components;++i) {
      printf(", %.1f", data[i]);
    }
  }
};

template<int T>
struct param_printer<T,
  typename std::enable_if<
    std::is_integral<
      typename anari::ANARITypeProperties<T>::base_type
    >::value
  >::type> {
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem) {
    const base_type *data = (base_type*)mem;
    printf("%lld", (long long)data[0]);
    for(int i = 1;i<components;++i) {
      printf(", %lld", (long long)data[i]);
    }
  }
};

template<int T>
struct param_printer<T,
  typename std::enable_if<
    anari::isObject(T)
  >::type> {
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void*) {
    printf("%s", anari::toString(T));
  }
};

template<int T>
struct param_printer_wrapper : public param_printer<T> { };

static void printAnariFromMemory(ANARIDataType t, const void *mem) {
  anari::anariTypeInvoke<void, param_printer_wrapper>(t, mem);
}

void print_info(ANARILibrary lib, const char *device, const char *objname, ANARIDataType objtype, const char *paramname, ANARIDataType paramtype, const char *indent) {
  int32_t *required = (int32_t*)anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "required", ANARI_BOOL);
  if(required && *required) {
    printf("%srequired\n", indent);
  }

  const void *mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "default", paramtype);
  if(mem) {
    printf("%sdefault = ", indent);
    printAnariFromMemory(paramtype, mem);
    printf("\n");
  }

  mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "minimum", paramtype);
  if(mem) {
    printf("%sminimum = ", indent);
    printAnariFromMemory(paramtype, mem);
    printf("\n");
  }

  mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "maximum", paramtype);
  if(mem) {
    printf("%smaximum = ", indent);
    printAnariFromMemory(paramtype, mem);
    printf("\n");
  }

  mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "value", ANARI_STRING_LIST);
  if(mem) {
    const char **list = (const char **)mem;
    printf("%svalue =\n", indent);
    for(;*list != nullptr;++list) {
      printf("%s   \"%s\"\n", indent, *list);
    }
  }

  mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "elementType", ANARI_TYPE_LIST);
  if(mem) {
    const ANARIDataType *list = (const ANARIDataType*)mem;
    printf("%selementType =\n", indent);
    for(;*list != ANARI_UNKNOWN;++list) {
      printf("%s   %s\n", indent, anari::toString(*list));
    }
  }

  mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "description", ANARI_STRING);
  if(mem) {
    printf("%sdescription = \"%s\"\n", indent, (const char*)mem);
  }

  mem = anariGetParameterInfo(lib, device, objname, objtype, paramname, paramtype, "sourceFeature", ANARI_STRING);
  if(mem) {
    printf("%ssourceFeature = %s\n", indent, (const char*)mem);
  }
}


/******************************************************************/
int main(int argc, const char **argv)
{
  const char *libraryName = NULL;
  const char *typeFilter = NULL;
  const char *subtypeFilter = NULL;
  bool skipParameters = false;
  bool info = false;
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
    if(strncmp(argv[i], "-i", 2) == 0) {
      info = true;
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

  printf("SDK version: %i.%i.%i\n",
    ANARI_SDK_VERSION_MAJOR,
    ANARI_SDK_VERSION_MINOR,
    ANARI_SDK_VERSION_PATCH
  );

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
    for(size_t j = 0;j<sizeof(namedTypes)/sizeof(ANARIDataType);++j) {
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
      for(size_t j = 0;j<sizeof(namedTypes)/sizeof(ANARIDataType);++j) {
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
            const ANARIParameter *params = (const ANARIParameter*)anariGetObjectInfo(lib, devices[i], types[k], namedTypes[j], "parameter", ANARI_PARAMETER_LIST);
            if(params) {
              for(int l = 0;params[l].name;++l){
                printf("         * %-25s %-25s\n", params[l].name, anari::toString(params[l].type));
                if(info) {
                  print_info(lib, devices[i], types[k], namedTypes[j], params[l].name, params[l].type, "            ");
                }
              }
            }
          }
        }
      }

      if(subtypeFilter == nullptr) {
        for(size_t j = 0;j<sizeof(anonymousTypes)/sizeof(ANARIDataType);++j) {
          if(typeFilter && strstr(anari::toString(anonymousTypes[j]), typeFilter) == nullptr) {
            continue;
          }

          printf("      %s:\n", anari::toString(anonymousTypes[j]));
          const ANARIParameter *params = (const ANARIParameter*)anariGetObjectInfo(lib, devices[i], 0, anonymousTypes[j], "parameter", ANARI_PARAMETER_LIST);
          if(params) {
            for(int l = 0;params[l].name;++l){
              printf("         * %-25s %-25s\n", params[l].name, anari::toString(params[l].type));
              if(info) {
                print_info(lib, devices[i], nullptr, anonymousTypes[j], params[l].name, params[l].type, "            ");
              }
            }
          }
        }
      }
    }
  }
  anariUnloadLibrary(lib);

  return 0;
}
