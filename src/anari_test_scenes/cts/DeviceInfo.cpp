// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "DeviceInfo.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace anari {
namespace cts {
namespace {

// Object types that advertise named subtypes (camera "perspective",
// material "matte", ...).
const ANARIDataType kNamedTypes[] = {ANARI_CAMERA,
    ANARI_INSTANCE,
    ANARI_GEOMETRY,
    ANARI_LIGHT,
    ANARI_MATERIAL,
    ANARI_RENDERER,
    ANARI_SAMPLER,
    ANARI_SPATIAL_FIELD,
    ANARI_VOLUME};

// Object types with a single anonymous form (device, arrays, world, frame).
const ANARIDataType kAnonymousTypes[] = {ANARI_DEVICE,
    ANARI_ARRAY1D,
    ANARI_ARRAY2D,
    ANARI_ARRAY3D,
    ANARI_SURFACE,
    ANARI_GROUP,
    ANARI_WORLD,
    ANARI_FRAME};

// --- Typed value formatting (ported from the legacy cts/src/anariInfo.cpp) ---
//
// anariGetParameterInfo returns raw memory for default/minimum/maximum keyed by
// the parameter's ANARIDataType. ParamPrinter renders that memory by type: the
// primary template prints nothing (types we don't format), with partial
// specializations for strings, floating-point vectors, integral vectors, and
// object-handle types. anariTypeInvoke dispatches on the runtime type.

template <int T, class Enable = void>
struct ParamPrinter
{
  void operator()(const void *, std::ostream &) {}
};

template <>
struct ParamPrinter<ANARI_STRING, void>
{
  void operator()(const void *mem, std::ostream &s)
  {
    s << static_cast<const char *>(mem);
  }
};

template <int T>
struct ParamPrinter<T,
    typename std::enable_if<std::is_floating_point<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem, std::ostream &s)
  {
    const base_type *data = static_cast<const base_type *>(mem);
    s << std::fixed << std::setprecision(1) << data[0];
    for (int i = 1; i < components; ++i)
      s << ", " << data[i];
  }
};

template <int T>
struct ParamPrinter<T,
    typename std::enable_if<std::is_integral<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using base_type = typename anari::ANARITypeProperties<T>::base_type;
  static const int components = anari::ANARITypeProperties<T>::components;
  void operator()(const void *mem, std::ostream &s)
  {
    const base_type *data = static_cast<const base_type *>(mem);
    s << static_cast<long long>(data[0]);
    for (int i = 1; i < components; ++i)
      s << ", " << static_cast<long long>(data[i]);
  }
};

template <int T>
struct ParamPrinter<T, typename std::enable_if<anari::isObject(T)>::type>
{
  void operator()(const void *, std::ostream &s)
  {
    s << anari::toString(T);
  }
};

template <int T>
struct ParamPrinterWrapper : public ParamPrinter<T>
{};

void printValue(ANARIDataType t, const void *mem, std::ostream &s)
{
  anari::anariTypeInvoke<void, ParamPrinterWrapper>(t, mem, s);
}

// Print the detailed metadata a device exposes for one parameter: whether it is
// required, its default/min/max, the allowed string or data-type values, the
// element types (for arrays), a description, and the feature that introduces it.
void printParameterInfo(ANARIDevice device,
    ANARIDataType objType,
    const char *objSubtype,
    const char *paramName,
    ANARIDataType paramType,
    const char *indent,
    std::ostream &s)
{
  const auto *required = static_cast<const int32_t *>(anariGetParameterInfo(
      device, objType, objSubtype, paramName, paramType, "required", ANARI_BOOL));
  if (required && *required)
    s << indent << "required\n";

  const void *mem = anariGetParameterInfo(
      device, objType, objSubtype, paramName, paramType, "default", paramType);
  if (mem) {
    s << indent << "default = ";
    printValue(paramType, mem, s);
    s << "\n";
  }

  mem = anariGetParameterInfo(
      device, objType, objSubtype, paramName, paramType, "minimum", paramType);
  if (mem) {
    s << indent << "minimum = ";
    printValue(paramType, mem, s);
    s << "\n";
  }

  mem = anariGetParameterInfo(
      device, objType, objSubtype, paramName, paramType, "maximum", paramType);
  if (mem) {
    s << indent << "maximum = ";
    printValue(paramType, mem, s);
    s << "\n";
  }

  mem = anariGetParameterInfo(device,
      objType,
      objSubtype,
      paramName,
      paramType,
      "value",
      ANARI_STRING_LIST);
  if (mem) {
    const auto *list = static_cast<const char *const *>(mem);
    s << indent << "value =\n";
    for (; *list != nullptr; ++list)
      s << indent << "   \"" << *list << "\"\n";
  }

  mem = anariGetParameterInfo(device,
      objType,
      objSubtype,
      paramName,
      paramType,
      "value",
      ANARI_DATA_TYPE_LIST);
  if (mem) {
    const auto *list = static_cast<const ANARIDataType *>(mem);
    s << indent << "value =\n";
    for (; *list != ANARI_UNKNOWN; ++list)
      s << indent << "   " << anari::toString(*list) << "\n";
  }

  mem = anariGetParameterInfo(device,
      objType,
      objSubtype,
      paramName,
      paramType,
      "elementType",
      ANARI_DATA_TYPE_LIST);
  if (mem) {
    const auto *list = static_cast<const ANARIDataType *>(mem);
    s << indent << "elementType =\n";
    for (; *list != ANARI_UNKNOWN; ++list)
      s << indent << "   " << anari::toString(*list) << "\n";
  }

  mem = anariGetParameterInfo(device,
      objType,
      objSubtype,
      paramName,
      paramType,
      "description",
      ANARI_STRING);
  if (mem)
    s << indent << "description = \"" << static_cast<const char *>(mem)
      << "\"\n";

  mem = anariGetParameterInfo(device,
      objType,
      objSubtype,
      paramName,
      paramType,
      "sourceFeature",
      ANARI_STRING);
  if (mem)
    s << indent << "sourceFeature = " << static_cast<const char *>(mem) << "\n";
}

bool contains(const std::string &haystack, const std::string &needle)
{
  return needle.empty() || haystack.find(needle) != std::string::npos;
}

// List a subtype's parameters (name + type), optionally with detailed info.
void printSubtypeParameters(ANARIDevice device,
    ANARIDataType objType,
    const char *objSubtype,
    const char *bullet,
    bool info,
    std::ostream &s)
{
  const auto *params = static_cast<const ANARIParameter *>(anariGetObjectInfo(
      device, objType, objSubtype, "parameter", ANARI_PARAMETER_LIST));
  if (!params)
    return;
  for (int p = 0; params[p].name; ++p) {
    s << bullet << std::left << std::setw(32) << params[p].name << " "
      << anari::toString(params[p].type) << "\n";
    if (info)
      printParameterInfo(device,
          objType,
          objSubtype,
          params[p].name,
          params[p].type,
          "            ",
          s);
  }
}

} // namespace

std::string queryDeviceInfo(anari::Device device,
    const std::string &typeFilter,
    const std::string &subtypeFilter,
    bool skipParameters,
    bool info)
{
  std::ostringstream s;
  s << "SDK version: " << ANARI_SDK_VERSION_MAJOR << "."
    << ANARI_SDK_VERSION_MINOR << "." << ANARI_SDK_VERSION_PATCH << "\n";

  // Subtypes section: every named object type and the subtypes it advertises.
  s << "Subtypes:\n";
  for (const auto objType : kNamedTypes) {
    const auto typeName = std::string(anari::toString(objType));
    if (!contains(typeName, typeFilter))
      continue;
    s << "   " << typeName << ": ";
    const char **subtypes = anariGetObjectSubtypes(device, objType);
    for (int i = 0; subtypes && subtypes[i]; ++i)
      s << subtypes[i] << " ";
    s << "\n";
  }

  if (skipParameters)
    return s.str();

  // Parameters section: each named subtype, then the anonymous object types.
  s << "Parameters:\n";
  for (const auto objType : kNamedTypes) {
    const auto typeName = std::string(anari::toString(objType));
    if (!contains(typeName, typeFilter))
      continue;
    const char **subtypes = anariGetObjectSubtypes(device, objType);
    for (int i = 0; subtypes && subtypes[i]; ++i) {
      if (!contains(subtypes[i], subtypeFilter))
        continue;
      s << "   " << typeName << " " << subtypes[i] << ":\n";
      printSubtypeParameters(device, objType, subtypes[i], "      * ", info, s);
    }
  }

  // Anonymous types have no subtypes; only when no subtype filter is active.
  if (subtypeFilter.empty()) {
    for (const auto objType : kAnonymousTypes) {
      const auto typeName = std::string(anari::toString(objType));
      if (!contains(typeName, typeFilter))
        continue;
      s << "   " << typeName << ":\n";
      printSubtypeParameters(device, objType, nullptr, "      * ", info, s);
    }
  }

  return s.str();
}

} // namespace cts
} // namespace anari
