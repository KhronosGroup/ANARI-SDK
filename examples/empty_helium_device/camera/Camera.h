// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Camera : public Object
{
  Camera(HeCoreDeviceGlobalState *s);
  ~Camera() override = default;
  static Camera *createInstance(
      std::string_view type, HeCoreDeviceGlobalState *state);
};

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Camera *, ANARI_CAMERA);
