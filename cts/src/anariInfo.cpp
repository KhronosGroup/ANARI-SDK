// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// anariInfo.cpp
//
// anariInfo opens a library and displays queryable information without
// creating any devices.



//#ifdef _WIN32
//#include <malloc.h>
//#else
//#include <alloca.h>
//#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <type_traits>

// anari
#include "anari/anari.h"
#include "anari/anari_cpp.hpp"
#include "anari/type_utility.h"

#include "anariWrapper.h"

namespace cts {

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

const ANARIDataType namedTypes[] = {ANARI_CAMERA,
    ANARI_GEOMETRY,
    ANARI_LIGHT,
    ANARI_MATERIAL,
    ANARI_RENDERER,
    ANARI_SAMPLER,
    ANARI_SPATIAL_FIELD,
    ANARI_VOLUME};

template <int T, class Enable = void>
struct param_printer
{
  void operator()(const void *) {}
};

template <>
struct param_printer<ANARI_STRING, void>
{
  void operator()(const void *mem)
  {
    const char *str = (const char *)mem;
    printf("\"%s\"", str);
  }
};

template <int T>
struct param_printer<T,
    typename std::enable_if<std::is_floating_point<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem)
  {
    const base_type *data = (base_type *)mem;
    printf("%.1f", data[0]);
    for (int i = 1; i < components; ++i) {
      printf(", %.1f", data[i]);
    }
  }
};

template <int T>
struct param_printer<T,
    typename std::enable_if<std::is_integral<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem)
  {
    const base_type *data = (base_type *)mem;
    printf("%lld", (long long)data[0]);
    for (int i = 1; i < components; ++i) {
      printf(", %lld", (long long)data[i]);
    }
  }
};

template <int T>
struct param_printer<T, typename std::enable_if<anari::isObject(T)>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *)
  {
    printf("%s", anari::toString(T));
  }
};

template <int T>
struct param_printer_wrapper : public param_printer<T>
{
};

static void printAnariFromMemory(ANARIDataType t, const void *mem)
{
  anari::anariTypeInvoke<void, param_printer_wrapper>(t, mem);
}

void print_info(ANARILibrary lib,
    const char *device,
    const char *objname,
    ANARIDataType objtype,
    const char *paramname,
    ANARIDataType paramtype,
    const char *indent)
{
  int32_t *required = (int32_t *)anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "required",
      ANARI_BOOL);
  if (required && *required) {
    printf("%srequired\n", indent);
  }

  const void *mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "default",
      paramtype);
  if (mem) {
    printf("%sdefault = ", indent);
    printAnariFromMemory(paramtype, mem);
    printf("\n");
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "minimum",
      paramtype);
  if (mem) {
    printf("%sminimum = ", indent);
    printAnariFromMemory(paramtype, mem);
    printf("\n");
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "maximum",
      paramtype);
  if (mem) {
    printf("%smaximum = ", indent);
    printAnariFromMemory(paramtype, mem);
    printf("\n");
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "value",
      ANARI_STRING_LIST);
  if (mem) {
    const char **list = (const char **)mem;
    printf("%svalue =\n", indent);
    for (; *list != nullptr; ++list) {
      printf("%s   \"%s\"\n", indent, *list);
    }
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "value",
      ANARI_DATA_TYPE_LIST);
  if (mem) {
    const ANARIDataType *list = (const ANARIDataType *)mem;
    printf("%svalue =\n", indent);
    for (; *list != ANARI_UNKNOWN; ++list) {
      printf("%s   %s\n", indent, anari::toString(*list));
    }
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "elementType",
      ANARI_DATA_TYPE_LIST);
  if (mem) {
    const ANARIDataType *list = (const ANARIDataType *)mem;
    printf("%selementType =\n", indent);
    for (; *list != ANARI_UNKNOWN; ++list) {
      printf("%s   %s\n", indent, anari::toString(*list));
    }
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "description",
      ANARI_STRING);
  if (mem) {
    printf("%sdescription = \"%s\"\n", indent, (const char *)mem);
  }

  mem = anariGetParameterInfo(lib,
      device,
      objname,
      objtype,
      paramname,
      paramtype,
      "sourceFeature",
      ANARI_STRING);
  if (mem) {
    printf("%ssourceFeature = %s\n", indent, (const char *)mem);
  }
}

/******************************************************************/
void queryInfo(const std::string &library,
    const std::function<void(const std::string message)> &callback)
{

  printf("SDK version: %i.%i.%i\n",
      ANARI_SDK_VERSION_MAJOR,
      ANARI_SDK_VERSION_MINOR,
      ANARI_SDK_VERSION_PATCH);

  ANARILibrary lib = anariLoadLibrary(library.c_str(), statusFunc, &callback);

  const char **devices = anariGetDeviceSubtypes(lib);
  if (devices) {
    printf("Devices:\n");
    for (int i = 0; devices[i]; ++i) {
      printf("   %s\n", devices[i]);
    }
  }

  for (int i = 0; devices[i]; ++i) {
    printf("Device \"%s\":\n", devices[i]);
    printf("   Subtypes:\n");
    for (size_t j = 0; j < sizeof(namedTypes) / sizeof(ANARIDataType); ++j) {
      const char **types =
          anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
      // print subtypes of named types
      printf("      %s: ", anari::toString(namedTypes[j]));
      if (types) {
        for (int k = 0; types[k]; ++k) {
          printf("%s ", types[k]);
        }
      }
      printf("\n");
    }

      printf("   Parameters:\n");
      for (size_t j = 0; j < sizeof(namedTypes) / sizeof(ANARIDataType); ++j) {

        const char **types =
            anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
        // print subtypes of named types
        if (types) {
          for (int k = 0; types[k]; ++k) {
            printf("      %s %s:\n", anari::toString(namedTypes[j]), types[k]);
            const ANARIParameter *params =
                (const ANARIParameter *)anariGetObjectInfo(lib,
                    devices[i],
                    types[k],
                    namedTypes[j],
                    "parameter",
                    ANARI_PARAMETER_LIST);
            if (params) {
              for (int l = 0; params[l].name; ++l) {
                printf("         * %-25s %-25s\n",
                    params[l].name,
                    anari::toString(params[l].type));
                  print_info(lib,
                      devices[i],
                      types[k],
                      namedTypes[j],
                      params[l].name,
                      params[l].type,
                      "            ");
                
              }
            }
          }
        }
      }

        for (size_t j = 0; j < sizeof(anonymousTypes) / sizeof(ANARIDataType);
             ++j) {

          printf("      %s:\n", anari::toString(anonymousTypes[j]));
          const ANARIParameter *params =
              (const ANARIParameter *)anariGetObjectInfo(lib,
                  devices[i],
                  0,
                  anonymousTypes[j],
                  "parameter",
                  ANARI_PARAMETER_LIST);
          if (params) {
            for (int l = 0; params[l].name; ++l) {
              printf("         * %-25s %-25s\n",
                  params[l].name,
                  anari::toString(params[l].type));

                print_info(lib,
                    devices[i],
                    nullptr,
                    anonymousTypes[j],
                    params[l].name,
                    params[l].type,
                    "            ");
              
            }
          }
        
      
    }
  }
  anariUnloadLibrary(lib);
}
} // namespace cts