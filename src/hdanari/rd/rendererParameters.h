// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// A single renderer parameter discovered through ANARI introspection
// (anariGetObjectInfo / anariGetParameterInfo). Defaults and bounds are
// resolved from the device, so the delegate never hardcodes them.
struct HdAnariRendererParam
{
  std::string name; // verbatim ANARI parameter name
  TfToken key; // == TfToken(name); the Hydra render-setting key
  ANARIDataType type{ANARI_UNKNOWN};
  VtValue defaultValue;
  bool hasDeviceDefault{false}; // false when defaultValue is a fabricated zero
  VtValue minValue; // empty when the device reports no minimum
  VtValue maxValue; // empty when the device reports no maximum
  std::string description;
  // Valid values when this is a string-enum parameter, else empty.
  std::vector<std::string> stringValues;
};

using HdAnariRendererParamList = std::vector<HdAnariRendererParam>;

// Parameters of a renderer subtype, with defaults/bounds/descriptions resolved
// from device introspection. Parameters whose ANARI type has no VtValue mapping
// (objects, arrays, ...) and parameters hdanari drives internally are skipped.
HdAnariRendererParamList HdAnariQueryRendererParameters(
    anari::Device device, const char *subtype);

// Forward a host-provided render-setting value to an ANARI renderer parameter,
// casting the VtValue to the parameter's introspected ANARI type. Returns false
// when the value cannot be represented as that type.
bool HdAnariSetRendererParameter(anari::Device device,
    anari::Renderer renderer,
    const HdAnariRendererParam &param,
    const VtValue &value);

PXR_NAMESPACE_CLOSE_SCOPE
