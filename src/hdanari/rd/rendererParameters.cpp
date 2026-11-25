// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "rendererParameters.h"

// Provides the anari::ANARITypeFor<GfVec*> specializations that
// anari::setParameter resolves implicitly below. // IWYU pragma: keep
#include "anariTypes.h"

#include <anari/anari.h>
#include <anari/frontend/anari_enums.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>

#include <cstdint>
#include <set>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Renderer parameters never auto-exposed nor auto-forwarded.
//   background: driven from the Hydra color AOV clear value.
//   name:       the generic ANARI object name, meaningless as a render setting.
const std::set<std::string> kInternallyManagedParameters = {
    "background",
    "name",
};

// Map ANARI parameter memory into a VtValue of the matching USD type. Returns
// an empty VtValue for types hdanari does not expose as render settings.
VtValue AnariMemoryToVtValue(ANARIDataType type, const void *mem)
{
  if (!mem)
    return {};

  switch (type) {
  case ANARI_BOOL:
    return VtValue(*static_cast<const int8_t *>(mem) != 0);
  case ANARI_INT32:
    return VtValue(*static_cast<const int32_t *>(mem));
  case ANARI_UINT32:
    return VtValue(*static_cast<const uint32_t *>(mem));
  case ANARI_FLOAT32:
    return VtValue(*static_cast<const float *>(mem));
  case ANARI_FLOAT64:
    return VtValue(*static_cast<const double *>(mem));
  case ANARI_FLOAT32_VEC2:
    return VtValue(GfVec2f(static_cast<const float *>(mem)));
  case ANARI_FLOAT32_VEC3:
    return VtValue(GfVec3f(static_cast<const float *>(mem)));
  case ANARI_FLOAT32_VEC4:
    return VtValue(GfVec4f(static_cast<const float *>(mem)));
  case ANARI_INT32_VEC2:
    return VtValue(GfVec2i(static_cast<const int32_t *>(mem)));
  case ANARI_INT32_VEC3:
    return VtValue(GfVec3i(static_cast<const int32_t *>(mem)));
  case ANARI_INT32_VEC4:
    return VtValue(GfVec4i(static_cast<const int32_t *>(mem)));
  case ANARI_STRING:
    return VtValue(std::string(static_cast<const char *>(mem)));
  default:
    return {};
  }
}

// A zero/empty VtValue of the USD type matching an ANARI type. Used as the
// render-setting default when the device reports none. Empty result marks the
// ANARI type as unsupported.
VtValue ZeroVtValueForType(ANARIDataType type)
{
  switch (type) {
  case ANARI_BOOL:
    return VtValue(false);
  case ANARI_INT32:
    return VtValue(int32_t(0));
  case ANARI_UINT32:
    return VtValue(uint32_t(0));
  case ANARI_FLOAT32:
    return VtValue(0.0f);
  case ANARI_FLOAT64:
    return VtValue(0.0);
  case ANARI_FLOAT32_VEC2:
    return VtValue(GfVec2f(0.0f));
  case ANARI_FLOAT32_VEC3:
    return VtValue(GfVec3f(0.0f));
  case ANARI_FLOAT32_VEC4:
    return VtValue(GfVec4f(0.0f));
  case ANARI_INT32_VEC2:
    return VtValue(GfVec2i(0));
  case ANARI_INT32_VEC3:
    return VtValue(GfVec3i(0));
  case ANARI_INT32_VEC4:
    return VtValue(GfVec4i(0));
  case ANARI_STRING:
    return VtValue(std::string());
  default:
    return {};
  }
}

const void *QueryParameterInfo(anari::Device d,
    const char *subtype,
    const ANARIParameter &param,
    const char *infoName,
    ANARIDataType infoType)
{
  return anariGetParameterInfo(
      d, ANARI_RENDERER, subtype, param.name, param.type, infoName, infoType);
}

template <typename T>
bool CastAndSet(anari::Device d,
    anari::Renderer r,
    const char *name,
    const VtValue &value)
{
  const VtValue cast = VtValue::Cast<T>(value);
  if (cast.IsEmpty())
    return false;
  anari::setParameter(d, r, name, cast.UncheckedGet<T>());
  return true;
}

} // namespace

HdAnariRendererParamList HdAnariQueryRendererParameters(
    anari::Device device, const char *subtype)
{
  HdAnariRendererParamList retval;

  const auto *params = static_cast<const ANARIParameter *>(anariGetObjectInfo(
      device, ANARI_RENDERER, subtype, "parameter", ANARI_PARAMETER_LIST));

  // A parameter name may be listed more than once with different ANARI types
  // (e.g. VisRTX's vec4 vs. array2d "background"); keep the first mappable one.
  std::set<std::string> seen;

  for (; params && params->name != nullptr; ++params) {
    if (kInternallyManagedParameters.count(params->name))
      continue;
    if (seen.count(params->name))
      continue;

    HdAnariRendererParam p;
    p.name = params->name;
    p.type = params->type;

    // Skip parameters whose ANARI type has no VtValue mapping.
    if (ZeroVtValueForType(params->type).IsEmpty())
      continue;

    const void *defaultMem =
        QueryParameterInfo(device, subtype, *params, "default", params->type);
    // When the device reports no default we fabricate a zero so the setting has
    // a value, but flag it so it is not forwarded back over the device's own
    // implicit default (see the forwarding loop in renderPass).
    p.hasDeviceDefault = defaultMem != nullptr;
    p.defaultValue = defaultMem ? AnariMemoryToVtValue(params->type, defaultMem)
                                : ZeroVtValueForType(params->type);

    p.key = TfToken(p.name);

    if (const void *minMem = QueryParameterInfo(
            device, subtype, *params, "minimum", params->type))
      p.minValue = AnariMemoryToVtValue(params->type, minMem);
    if (const void *maxMem = QueryParameterInfo(
            device, subtype, *params, "maximum", params->type))
      p.maxValue = AnariMemoryToVtValue(params->type, maxMem);

    if (const auto *description = static_cast<const char *>(QueryParameterInfo(
            device, subtype, *params, "description", ANARI_STRING)))
      p.description = description;

    const auto *const *values =
        static_cast<const char *const *>(QueryParameterInfo(
            device, subtype, *params, "value", ANARI_STRING_LIST));
    for (; values && *values; ++values)
      p.stringValues.emplace_back(*values);

    seen.insert(p.name);
    retval.push_back(std::move(p));
  }

  return retval;
}

bool HdAnariSetRendererParameter(anari::Device d,
    anari::Renderer r,
    const HdAnariRendererParam &param,
    const VtValue &value)
{
  const char *name = param.name.c_str();
  switch (param.type) {
  case ANARI_BOOL:
    return CastAndSet<bool>(d, r, name, value);
  case ANARI_INT32:
    return CastAndSet<int32_t>(d, r, name, value);
  case ANARI_UINT32:
    return CastAndSet<uint32_t>(d, r, name, value);
  case ANARI_FLOAT32:
    return CastAndSet<float>(d, r, name, value);
  case ANARI_FLOAT64:
    return CastAndSet<double>(d, r, name, value);
  case ANARI_FLOAT32_VEC2:
    return CastAndSet<GfVec2f>(d, r, name, value);
  case ANARI_FLOAT32_VEC3:
    return CastAndSet<GfVec3f>(d, r, name, value);
  case ANARI_FLOAT32_VEC4:
    return CastAndSet<GfVec4f>(d, r, name, value);
  case ANARI_INT32_VEC2:
    return CastAndSet<GfVec2i>(d, r, name, value);
  case ANARI_INT32_VEC3:
    return CastAndSet<GfVec3i>(d, r, name, value);
  case ANARI_INT32_VEC4:
    return CastAndSet<GfVec4i>(d, r, name, value);
  case ANARI_STRING:
    if (value.IsHolding<std::string>()) {
      anari::setParameter(d, r, name, value.UncheckedGet<std::string>().c_str());
      return true;
    }
    if (value.IsHolding<TfToken>()) {
      anari::setParameter(d, r, name, value.UncheckedGet<TfToken>().GetText());
      return true;
    }
    return false;
  default:
    return false;
  }
}

PXR_NAMESPACE_CLOSE_SCOPE
