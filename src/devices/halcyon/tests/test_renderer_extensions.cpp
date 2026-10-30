// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Each renderer reports the extensions it honours (AGENTS.md, "Parameters
// and properties"): the 'preview' renderer doesn't claim the lights,
// KHR_FRAME_ACCUMULATION or KHR_CAMERA_DEPTH_OF_FIELD, which only the
// 'quality' reads, but the hdri light, which its camera misses show; both
// claim the volume extensions.
// Both introspection (anariGetObjectInfo's "extension") and a renderer's
// "extension" property report them, and the device's list is their union.
// The renderer subtypes are 'preview' and 'quality', in that order;
// 'default', the spec's alias of the first, isn't listed, but creates
// 'preview' and introspection answers it as 'preview'. An unknown subtype
// gets an object that is never valid and reports no extensions.

// anari
#define ANARI_EXTENSION_UTILITY_IMPL
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

int g_errors = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_FATAL_ERROR
      || severity == ANARI_SEVERITY_ERROR
      || severity == ANARI_SEVERITY_WARNING) {
    std::fprintf(stderr, "[ANARI] %s\n", message);
    g_errors++;
  }
}

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

using Extensions = std::set<std::string>;

// Both renderers honour these
const Extensions COMMON = {
    "ANARI_KHR_CAMERA_ORTHOGRAPHIC",
    "ANARI_KHR_CAMERA_PERSPECTIVE",
    "ANARI_KHR_DEVICE_SYNCHRONIZATION",
    "ANARI_KHR_FRAME_CHANNEL_ALBEDO",
    "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID",
    "ANARI_KHR_FRAME_CHANNEL_NORMAL",
    "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID",
    "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID",
    "ANARI_KHR_FRAME_COMPLETION_CALLBACK",
    "ANARI_KHR_GEOMETRY_CONE",
    "ANARI_KHR_GEOMETRY_CURVE",
    "ANARI_KHR_GEOMETRY_CYLINDER",
    "ANARI_KHR_GEOMETRY_QUAD",
    "ANARI_KHR_GEOMETRY_SPHERE",
    "ANARI_KHR_GEOMETRY_TRIANGLE",
    "ANARI_KHR_INSTANCE_TRANSFORM",
    "ANARI_KHR_INSTANCE_TRANSFORM_ARRAY",
    // the 'preview' renderer isn't lit by it, but shows it where rays miss
    "ANARI_KHR_LIGHT_HDRI",
    "ANARI_KHR_MATERIAL_MATTE",
    "ANARI_KHR_MATERIAL_PHYSICALLY_BASED",
    "ANARI_KHR_RENDERER_AMBIENT_LIGHT",
    "ANARI_KHR_RENDERER_BACKGROUND_COLOR",
    "ANARI_KHR_RENDERER_BACKGROUND_IMAGE",
    "ANARI_KHR_SAMPLER_IMAGE1D",
    "ANARI_KHR_SAMPLER_IMAGE2D",
    "ANARI_KHR_SAMPLER_IMAGE3D",
    "ANARI_KHR_SAMPLER_PRIMITIVE",
    "ANARI_KHR_SAMPLER_TRANSFORM",
    "ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
    "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
    "ANARI_HALCYON_GEOMETRY_CURVE_BASIS",
    "ANARI_HALCYON_MATERIAL_MATERIALX",
    "ANARI_HALCYON_MATERIAL_OPENPBR",
    "ANARI_HALCYON_MATERIAL_USD_PREVIEW_SURFACE",
};

// Only the 'quality' is lit by lights, refines its image and samples the
// camera's lens
const Extensions PATHTRACER_ONLY = {
    "ANARI_KHR_CAMERA_DEPTH_OF_FIELD",
    "ANARI_KHR_FRAME_ACCUMULATION",
    "ANARI_KHR_LIGHT_DIRECTIONAL",
    "ANARI_KHR_LIGHT_POINT",
    "ANARI_KHR_LIGHT_PRIMARY_VISIBILITY",
    "ANARI_KHR_LIGHT_QUAD",
    "ANARI_KHR_LIGHT_RING",
    "ANARI_KHR_LIGHT_SPOT",
};

Extensions operator+(Extensions a, const Extensions &b)
{
  a.insert(b.begin(), b.end());
  return a;
}

// 'list' as a set, and whether it had no duplicates
Extensions toSet(const char *const *list, bool *unique = nullptr)
{
  Extensions result;
  size_t n = 0;
  for (; list && list[n]; n++)
    result.insert(list[n]);
  if (unique)
    *unique = result.size() == n;
  return result;
}

std::string describe(const Extensions &got, const Extensions &expected)
{
  std::string s;
  for (const auto &e : got)
    if (!expected.count(e))
      s += " +" + e;
  for (const auto &e : expected)
    if (!got.count(e))
      s += " -" + e;
  return s;
}

bool checkList(const std::string &name,
    const char *const *list,
    const Extensions &expected)
{
  bool unique = false;
  const Extensions got = toSet(list, &unique);
  const std::string diff = describe(got, expected);
  if (!diff.empty())
    std::fprintf(stderr, "%s:%s\n", name.c_str(), diff.c_str());
  return report(name, list && unique && got == expected);
}

const char *const *objectInfoExtensions(anari::Device d, const char *subtype)
{
  return (const char *const *)anariGetObjectInfo(
      d, ANARI_RENDERER, subtype, "extension", ANARI_STRING_LIST);
}

const char *const *propertyExtensions(anari::Device d, anari::Renderer r)
{
  const char *const *list = nullptr;
  const int found = anariGetProperty(d,
      r,
      "extension",
      ANARI_STRING_LIST,
      &list,
      sizeof(list),
      ANARI_WAIT);
  return found ? list : nullptr;
}

bool testIntrospection(anari::Library library, anari::Device d)
{
  const Extensions previewExtensions = COMMON;
  const Extensions pathtracerExtensions = COMMON + PATHTRACER_ONLY;

  const char **subtypes = anariGetObjectSubtypes(d, ANARI_RENDERER);
  bool pass = report("renderer subtypes are 'preview', 'quality'",
      subtypes && subtypes[0] && std::string(subtypes[0]) == "preview"
          && subtypes[1] && std::string(subtypes[1]) == "quality"
          && !subtypes[2]);
  pass &= checkList("'default' renderer info is 'preview''s",
      objectInfoExtensions(d, "default"),
      previewExtensions);
  auto *blurPasses = (const int32_t *)anariGetParameterInfo(d,
      ANARI_RENDERER,
      "default",
      "ambientOcclusionBlurPasses",
      ANARI_INT32,
      "default",
      ANARI_INT32);
  pass &= report("'default' renderer parameters are 'preview''s",
      blurPasses && *blurPasses == 1);

  pass &= checkList("'preview' renderer info lists what it honours",
      objectInfoExtensions(d, "preview"),
      previewExtensions);
  pass &= checkList("'quality' renderer info lists what it honours",
      objectInfoExtensions(d, "quality"),
      pathtracerExtensions);

  // As apps read them through the SDK's extension structs
  const anari::Extensions previewStruct =
      anari::extension::getObjectExtensionStruct(d, ANARI_RENDERER, "preview");
  const anari::Extensions pathtracerStruct =
      anari::extension::getObjectExtensionStruct(d, ANARI_RENDERER, "quality");
  pass &= report(
      "extension structs: lights only for the 'quality', but the hdri",
      !previewStruct.ANARI_KHR_LIGHT_POINT
          && previewStruct.ANARI_KHR_LIGHT_HDRI
          && !previewStruct.ANARI_KHR_LIGHT_PRIMARY_VISIBILITY
          && !previewStruct.ANARI_KHR_FRAME_ACCUMULATION
          && previewStruct.ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D
          && pathtracerStruct.ANARI_KHR_LIGHT_POINT
          && pathtracerStruct.ANARI_KHR_LIGHT_HDRI
          && pathtracerStruct.ANARI_KHR_LIGHT_PRIMARY_VISIBILITY
          && pathtracerStruct.ANARI_KHR_FRAME_ACCUMULATION
          && pathtracerStruct.ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D);

  // The device implements every extension one of its renderers honours
  const Extensions all = previewExtensions + pathtracerExtensions;
  const char *const *deviceProperty = nullptr;
  anariGetProperty(d,
      d,
      "extension",
      ANARI_STRING_LIST,
      &deviceProperty,
      sizeof(deviceProperty),
      ANARI_WAIT);
  pass &= checkList(
      "device 'extension' property is the renderers' union", deviceProperty, all);
  pass &= checkList("anariGetDeviceExtensions is the renderers' union",
      anariGetDeviceExtensions(library, "default"),
      all);
  pass &= checkList("device object info is the renderers' union",
      (const char *const *)anariGetObjectInfo(
          d, ANARI_DEVICE, nullptr, "extension", ANARI_STRING_LIST),
      all);
  return pass;
}

bool testProperty(anari::Device d)
{
  bool pass = true;
  const struct
  {
    const char *subtype;
    const char *reportedAs;
  } cases[] = {
      {"preview", "preview"},
      {"quality", "quality"},
      // 'default' gets the 'preview' renderer
      {"default", "preview"},
  };
  for (const auto &c : cases) {
    auto r = anari::newObject<anari::Renderer>(d, c.subtype);
    anari::commitParameters(d, r);
    pass &= checkList(std::string("'") + c.subtype
            + "' renderer's 'extension' property is '" + c.reportedAs
            + "''s info",
        propertyExtensions(d, r),
        toSet(objectInfoExtensions(d, c.reportedAs)));
    anari::release(d, r);
  }

  auto unknown = anari::newObject<anari::Renderer>(d, "noSuchRenderer");
  anari::commitParameters(d, unknown);
  pass &= report("an unknown renderer subtype reports no extensions",
      propertyExtensions(d, unknown) == nullptr);
  anari::release(d, unknown);
  return pass;
}

} // namespace

int main()
{
  auto library = anari::loadLibrary("halcyon", statusFunc);
  if (!library) {
    std::fprintf(stderr, "failed to load halcyon library\n");
    return 1;
  }
  auto d = anari::newDevice(library, "default");

  bool pass = testIntrospection(library, d);
  pass &= testProperty(d);

  anari::release(d, d);
  anari::unloadLibrary(library);

  pass &= report("no errors or warnings", g_errors == 0);
  return pass ? 0 : 1;
}
