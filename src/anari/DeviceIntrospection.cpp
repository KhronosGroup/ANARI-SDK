// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/frontend/anari_device_introspection.h"

#include "anari/anari_cpp/Traits.h"
#include "anari/frontend/type_utility.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace anari {
namespace introspection {
namespace {

const ANARIDataType kNamedTypes[] = {ANARI_INSTANCE,
    ANARI_CAMERA,
    ANARI_GEOMETRY,
    ANARI_LIGHT,
    ANARI_MATERIAL,
    ANARI_RENDERER,
    ANARI_SAMPLER,
    ANARI_SPATIAL_FIELD,
    ANARI_VOLUME};

const ANARIDataType kAnonymousTypes[] = {ANARI_DEVICE,
    ANARI_ARRAY1D,
    ANARI_ARRAY2D,
    ANARI_ARRAY3D,
    ANARI_SURFACE,
    ANARI_GROUP,
    ANARI_WORLD,
    ANARI_FRAME};

bool contains(const std::string &haystack, const std::string &needle)
{
  return needle.empty() || haystack.find(needle) != std::string::npos;
}

template <int T, class Enable = void>
struct ParameterValueFormatter
{
  void operator()(const void *, std::ostream &) {}
};

template <>
struct ParameterValueFormatter<ANARI_STRING, void>
{
  void operator()(const void *value, std::ostream &out)
  {
    out << static_cast<const char *>(value);
  }
};

template <int T>
struct ParameterValueFormatter<T,
    typename std::enable_if<std::is_floating_point<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using BaseType = typename anari::ANARITypeProperties<T>::base_type;
  static constexpr int kComponents = anari::ANARITypeProperties<T>::components;

  void operator()(const void *value, std::ostream &out)
  {
    const auto *data = static_cast<const BaseType *>(value);
    out << std::fixed << std::setprecision(1) << data[0];
    for (int i = 1; i < kComponents; ++i)
      out << ", " << data[i];
  }
};

template <int T>
struct ParameterValueFormatter<T,
    typename std::enable_if<std::is_integral<
        typename anari::ANARITypeProperties<T>::base_type>::value>::type>
{
  using BaseType = typename anari::ANARITypeProperties<T>::base_type;
  static constexpr int kComponents = anari::ANARITypeProperties<T>::components;

  void operator()(const void *value, std::ostream &out)
  {
    const auto *data = static_cast<const BaseType *>(value);
    out << static_cast<long long>(data[0]);
    for (int i = 1; i < kComponents; ++i)
      out << ", " << static_cast<long long>(data[i]);
  }
};

template <int T>
struct ParameterValueFormatter<T,
    typename std::enable_if<anari::isObject(T)>::type>
{
  void operator()(const void *, std::ostream &out)
  {
    out << anari::toString(T);
  }
};

template <int T>
struct ParameterValueFormatterWrapper : public ParameterValueFormatter<T>
{
};

std::optional<std::string> queryValue(ANARIDevice device,
    ANARIDataType objectType,
    const char *objectSubtype,
    const ANARIParameter &parameter,
    const char *name)
{
  const void *value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      name,
      parameter.type);
  if (!value)
    return {};
  return formatParameterValue(parameter.type, value);
}

ParameterMetadata queryParameterMetadata(ANARIDevice device,
    ANARIDataType objectType,
    const char *objectSubtype,
    const ANARIParameter &parameter)
{
  ParameterMetadata metadata;
  const auto *required =
      static_cast<const int32_t *>(anariGetParameterInfo(device,
          objectType,
          objectSubtype,
          parameter.name,
          parameter.type,
          "required",
          ANARI_BOOL));
  metadata.required = required && *required;
  metadata.defaultValue =
      queryValue(device, objectType, objectSubtype, parameter, "default");
  metadata.minimum =
      queryValue(device, objectType, objectSubtype, parameter, "minimum");
  metadata.maximum =
      queryValue(device, objectType, objectSubtype, parameter, "maximum");

  const void *value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      "use",
      ANARI_STRING);
  if (value)
    metadata.use = static_cast<const char *>(value);

  value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      "value",
      ANARI_STRING_LIST);
  for (auto list = static_cast<const char *const *>(value); list && *list;
       ++list)
    metadata.stringValues.emplace_back(*list);

  value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      "value",
      ANARI_DATA_TYPE_LIST);
  for (auto list = static_cast<const ANARIDataType *>(value);
       list && *list != ANARI_UNKNOWN;
       ++list)
    metadata.dataTypeValues.push_back(*list);

  value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      "elementType",
      ANARI_DATA_TYPE_LIST);
  for (auto list = static_cast<const ANARIDataType *>(value);
       list && *list != ANARI_UNKNOWN;
       ++list)
    metadata.elementTypes.push_back(*list);

  value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      "description",
      ANARI_STRING);
  if (value)
    metadata.description = static_cast<const char *>(value);

  value = anariGetParameterInfo(device,
      objectType,
      objectSubtype,
      parameter.name,
      parameter.type,
      "sourceExtension",
      ANARI_STRING);
  if (value)
    metadata.sourceExtension = static_cast<const char *>(value);

  return metadata;
}

ObjectInfo queryObjectInfo(ANARIDevice device,
    ANARIDataType type,
    const char *subtype,
    bool includeParameterInfo)
{
  ObjectInfo object;
  object.type = type;
  if (subtype)
    object.subtype = subtype;

  const auto *parameters =
      static_cast<const ANARIParameter *>(anariGetObjectInfo(
          device, type, subtype, "parameter", ANARI_PARAMETER_LIST));
  for (int i = 0; parameters && parameters[i].name; ++i) {
    ParameterInfo parameter{parameters[i].name, parameters[i].type, {}};
    if (includeParameterInfo)
      parameter.metadata =
          queryParameterMetadata(device, type, subtype, parameters[i]);
    object.parameters.push_back(std::move(parameter));
  }
  return object;
}

} // namespace

DeviceInfo queryDeviceInfo(ANARIDevice device, const QueryOptions &options)
{
  DeviceInfo info;
  info.parametersIncluded = options.includeParameters;

  for (const auto type : kNamedTypes) {
    if (!contains(anari::toString(type), options.typeFilter))
      continue;

    ObjectTypeInfo objectType;
    objectType.type = type;
    const char **subtypes = anariGetObjectSubtypes(device, type);
    for (int i = 0; subtypes && subtypes[i]; ++i) {
      objectType.subtypes.emplace_back(subtypes[i]);
      if (options.includeParameters
          && contains(subtypes[i], options.subtypeFilter))
        info.objects.push_back(queryObjectInfo(
            device, type, subtypes[i], options.includeParameterInfo));
    }
    info.objectTypes.push_back(std::move(objectType));
  }

  if (options.includeParameters && options.subtypeFilter.empty()) {
    for (const auto type : kAnonymousTypes) {
      if (contains(anari::toString(type), options.typeFilter))
        info.objects.push_back(queryObjectInfo(
            device, type, nullptr, options.includeParameterInfo));
    }
  }

  const auto *channels = static_cast<const char *const *>(anariGetObjectInfo(
      device, ANARI_FRAME, nullptr, "channel", ANARI_STRING_LIST));
  for (int i = 0; channels && channels[i]; ++i)
    info.frameChannels.emplace_back(channels[i]);

  return info;
}

std::string formatParameterValue(ANARIDataType type, const void *value)
{
  std::ostringstream out;
  anari::anariTypeInvoke<void, ParameterValueFormatterWrapper>(
      type, value, out);
  return out.str();
}

std::string formatDeviceInfo(
    const DeviceInfo &info, const FormatOptions &options)
{
  std::ostringstream out;
  if (options.includeSdkVersion)
    out << "SDK version: " << ANARI_SDK_VERSION_MAJOR << "."
        << ANARI_SDK_VERSION_MINOR << "." << ANARI_SDK_VERSION_PATCH << "\n";

  out << options.indent << "Subtypes:\n";
  for (const auto &objectType : info.objectTypes) {
    out << options.indent << "   " << anari::toString(objectType.type) << ": ";
    for (const auto &subtype : objectType.subtypes)
      out << subtype << " ";
    out << "\n";
  }

  if (options.includeFrameChannels && !info.frameChannels.empty()) {
    out << options.indent << "Frame Channels:\n";
    for (const auto &channel : info.frameChannels)
      out << options.indent << "   " << channel << "\n";
  }

  if (!info.parametersIncluded)
    return out.str();

  out << options.indent << "Parameters:\n";
  for (const auto &object : info.objects) {
    out << options.indent << "   " << anari::toString(object.type);
    if (!object.subtype.empty())
      out << " " << object.subtype;
    out << ":\n";
    for (const auto &parameter : object.parameters) {
      out << options.indent << "      * " << std::left
          << std::setw(static_cast<int>(options.parameterNameWidth))
          << parameter.name << " ";
      if (options.parameterTypeWidth > 0)
        out << std::setw(static_cast<int>(options.parameterTypeWidth));
      out << anari::toString(parameter.type) << "\n";

      const auto &metadata = parameter.metadata;
      const std::string metadataIndent(12, ' ');
      if (metadata.required)
        out << metadataIndent << "required\n";
      const auto printValue = [&](const char *name,
                                  const std::optional<std::string> &value) {
        if (!value)
          return;
        out << metadataIndent << name << " = ";
        if (options.quoteStringValues && parameter.type == ANARI_STRING)
          out << '"' << *value << '"';
        else
          out << *value;
        out << "\n";
      };
      printValue("default", metadata.defaultValue);
      printValue("minimum", metadata.minimum);
      printValue("maximum", metadata.maximum);
      if (metadata.use) {
        out << metadataIndent << "use = ";
        if (options.quoteStringValues)
          out << '"' << *metadata.use << '"';
        else
          out << *metadata.use;
        out << "\n";
      }
      if (!metadata.stringValues.empty()) {
        out << metadataIndent << "value =\n";
        for (const auto &value : metadata.stringValues)
          out << metadataIndent << "   \"" << value << "\"\n";
      }
      if (!metadata.dataTypeValues.empty()) {
        out << metadataIndent << "value =\n";
        for (const auto value : metadata.dataTypeValues)
          out << metadataIndent << "   " << anari::toString(value) << "\n";
      }
      if (!metadata.elementTypes.empty()) {
        out << metadataIndent << "elementType =\n";
        for (const auto value : metadata.elementTypes)
          out << metadataIndent << "   " << anari::toString(value) << "\n";
      }
      if (metadata.description)
        out << metadataIndent << "description = \"" << *metadata.description
            << "\"\n";
      if (metadata.sourceExtension)
        out << metadataIndent
            << "sourceExtension = " << *metadata.sourceExtension << "\n";
    }
  }

  return out.str();
}

} // namespace introspection
} // namespace anari
