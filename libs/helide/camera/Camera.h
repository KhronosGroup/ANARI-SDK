// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace helide {

struct Camera : public Object
{
  static size_t objectCount();

  Camera(HelideGlobalState *s);
  ~Camera() override;

  virtual void commit() override;

  static Camera *createInstance(
      std::string_view type, HelideGlobalState *state);

  virtual Ray createRay(const float2 &screen) const = 0;

 protected:
  float3 m_pos;
  float3 m_dir;
  float3 m_up;
};

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Camera *, ANARI_CAMERA);
