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
#include <iomanip>
#include <sstream>

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
  void operator()(const void *, std::stringstream& s)
  {
  }
};

template <>
struct param_printer<ANARI_STRING, void>
{
  void operator()(const void *mem, std::stringstream &s)
  {
    s << (const char *)mem;
  }
};

template <int T>
struct param_printer<T,
    typename std::enable_if<std::is_floating_point<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem, std::stringstream &s)
  {
    const base_type *data = (base_type *)mem;
    s << std::fixed << std::setprecision(1) << data[0];
    for (int i = 1; i < components; ++i) {
      s << ", " << data[i];
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
  void operator()(const void *mem, std::stringstream &s)
  {
    const base_type *data = (base_type *)mem;
    s << (long long)data[0];
    for (int i = 1; i < components; ++i) {
      s << ", " << (long long)data[i];
    }
  }
};

template <int T>
struct param_printer<T, typename std::enable_if<anari::isObject(T)>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *, std::stringstream &s)
  {
    s << anari::toString(T);
  }
};

template <int T>
struct param_printer_wrapper : public param_printer<T>
{
};

static void printAnariFromMemory(ANARIDataType t, const void *mem, std::stringstream &s)
{
  anari::anariTypeInvoke<void, param_printer_wrapper>(t, mem, s);
}

void print_info(ANARILibrary lib,
    const char *device,
    const char *objname,
    ANARIDataType objtype,
    const char *paramname,
    ANARIDataType paramtype,
    const char *indent, std::stringstream& s)
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
    s << indent << "required\n";
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
    s << indent << "default = ";
    printAnariFromMemory(paramtype, mem, s);
    s << "\n";
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
    s << indent << "minimum = ";
    printAnariFromMemory(paramtype, mem, s);
    s << "\n";
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
    s << indent << "maximum = ";
    printAnariFromMemory(paramtype, mem, s);
    s << "\n";
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
    s << indent << "value =\n";
    for (; *list != nullptr; ++list) {
      s << indent << "   \"" << *list << "\"\n";
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
    s << indent << "value =\n ";
    for (; *list != ANARI_UNKNOWN; ++list) {
      s << indent << "   " << anari::toString(*list) << "\n ";
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
    s << indent << "elementType =\n";
    for (; *list != ANARI_UNKNOWN; ++list) {
      s << indent << "   " << anari::toString(*list) << "\n ";
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
    s << indent << "description = \"" <<(const char *)mem << "\"\n";
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
    s << indent << "sourceFeature = " << (const char *)mem << "\n ";
  }
}

/******************************************************************/
std::string queryInfo(const std::string &library,
    std::optional<std::string> typeFilter,
    std::optional<std::string> subtypeFilter,
    bool skipParameters,
    bool info,
    const std::optional<pybind11::function> &callback)
{
  std::stringstream s;
  s << "SDK version: " << ANARI_SDK_VERSION_MAJOR << "."
    << ANARI_SDK_VERSION_MINOR << "." << ANARI_SDK_VERSION_PATCH << "\n";

  anari::Library lib;
  if (callback.has_value()) {
    lib = anari::loadLibrary(library.c_str(), statusFunc, &(callback.value()));
  } else {
    lib = anari::loadLibrary(library.c_str(), statusFunc, nullptr);
  }

  if (lib == nullptr) {
    throw std::runtime_error("Library could not be loaded: " + library);
  }

  const char **devices = anariGetDeviceSubtypes(lib);
  if (devices) {
    s << "Devices:\n";
    for (int i = 0; devices[i]; ++i) {
      s << "   " << devices[i] << "\n";
    }
  }

  for (int i = 0; devices[i]; ++i) {
    s << "Device \"" << devices[i] <<"\":\n";
    s << "   Subtypes:\n";
    for (size_t j = 0; j < sizeof(namedTypes) / sizeof(ANARIDataType); ++j) {
      const char **types =
          anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
      // print subtypes of named types
      s << "      " << anari::toString(namedTypes[j]) << ": ";
      if (types) {
        for (int k = 0; types[k]; ++k) {
          s << types[k] << " ";
        }
      }
      s << "\n";
    }

    if (!skipParameters) {
      s << "   Parameters:\n";
      for (size_t j = 0; j < sizeof(namedTypes) / sizeof(ANARIDataType); ++j) {
        if (typeFilter
            && strstr(anari::toString(namedTypes[j]), typeFilter->c_str())
                == nullptr) {
          continue;
        }

        const char **types =
            anariGetObjectSubtypes(lib, devices[i], namedTypes[j]);
        // print subtypes of named types
        if (types) {
          for (int k = 0; types[k]; ++k) {
            if (subtypeFilter
                && strstr(types[k], subtypeFilter->c_str()) == nullptr) {
              continue;
            }
            s << "      " << anari::toString(namedTypes[j]) << " " << types[k]
              << ":\n";
            const ANARIParameter *params =
                (const ANARIParameter *)anariGetObjectInfo(lib,
                    devices[i],
                    types[k],
                    namedTypes[j],
                    "parameter",
                    ANARI_PARAMETER_LIST);
            if (params) {
              for (int l = 0; params[l].name; ++l) {
                s << "         * " << std::left << std::setw(32)
                  << params[l].name << " " << std::left << std::setw(32)
                  << anari::toString(params[l].type) << "\n";
                if (info) {
                  print_info(lib,
                      devices[i],
                      types[k],
                      namedTypes[j],
                      params[l].name,
                      params[l].type,
                      "            ", s);
                }
              }
            }
          }
        }
      }

      if (!subtypeFilter.has_value()) {
        for (size_t j = 0; j < sizeof(anonymousTypes) / sizeof(ANARIDataType);
             ++j) {
          if (typeFilter
              && strstr(anari::toString(anonymousTypes[j]), typeFilter->c_str())
                  == nullptr) {
            continue;
          }

          s << "      " << anari::toString(anonymousTypes[j]) << ":\n";
          const ANARIParameter *params =
              (const ANARIParameter *)anariGetObjectInfo(lib,
                  devices[i],
                  0,
                  anonymousTypes[j],
                  "parameter",
                  ANARI_PARAMETER_LIST);
          if (params) {
            for (int l = 0; params[l].name; ++l) {       
              s << "         * " << std::left << std::setw(32) << params[l].name
                << " "
                << std::left << std::setw(32) << anari::toString(params[l].type)
                << "\n";
              if (info) {
                print_info(lib,
                    devices[i],
                    nullptr,
                    anonymousTypes[j],
                    params[l].name,
                    params[l].type,
                    "            ", s);
              }
            }
          }
        }
      }
    }
  }
  anariUnloadLibrary(lib);
  return s.str();
}
} // namespace cts