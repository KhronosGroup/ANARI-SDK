// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Object.h"

namespace anari {
namespace example_device {

struct Camera : public Object
{
  Camera() = default;

  static FactoryMapPtr<Camera> g_cameras;
  static void init();
  static Camera *createInstance(const char *type);
  static ANARIParameter *parameters(const char *type);

  virtual void commit() override;

  virtual Ray createRay(const vec2 &screen) const = 0;

 protected:
  vec3 m_pos;
  vec3 m_dir;
  vec3 m_up;
};

} // namespace example_device

ANARI_TYPEFOR_SPECIALIZATION(example_device::Camera *, ANARI_CAMERA);

} // namespace anari
