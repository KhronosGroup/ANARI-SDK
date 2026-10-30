// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
#include "Camera_ispc.h"

namespace halcyon {

// Camera subtypes ////////////////////////////////////////////////////////////

struct PerspectiveCamera : public Camera
{
  PerspectiveCamera(HalcyonDeviceGlobalState *s) : Camera(s) {}

  void commitParameters() override
  {
    Camera::commitParameters();
    m_fovy = getParam<float>("fovy", anari::radians(60.f));
  }

  void finalize() override
  {
    Camera::finalize();
    ispc::Camera_setPerspectiveHeight(getIE(), 2.f * tanf(0.5f * m_fovy));
  }

 private:
  float m_fovy{0.f};
};

struct OrthographicCamera : public Camera
{
  OrthographicCamera(HalcyonDeviceGlobalState *s) : Camera(s) {}

  void commitParameters() override
  {
    Camera::commitParameters();
    m_height = getParam<float>("height", 1.f);
  }

  void finalize() override
  {
    Camera::finalize();
    ispc::Camera_setOrthographicHeight(getIE(), m_height);
  }

 private:
  float m_height{1.f};
};

// Camera /////////////////////////////////////////////////////////////////////

Camera::Camera(HalcyonDeviceGlobalState *s)
    : Object(ANARI_CAMERA, s, ispc::sizeof_Camera())
{}

Camera::~Camera() = default;

Camera *Camera::createInstance(
    std::string_view type, HalcyonDeviceGlobalState *s)
{
  if (type == "perspective")
    return new PerspectiveCamera(s);
  else if (type == "orthographic")
    return new OrthographicCamera(s);
  else
    return (Camera *)new UnknownObject(ANARI_CAMERA, s, ispc::sizeof_Camera());
}

void Camera::commitParameters()
{
  m_pos = getParam<float3>("position", float3(0.f));
  m_dir = normalize(getParam<float3>("direction", float3(0.f, 0.f, 1.f)));
  m_up = normalize(getParam<float3>("up", float3(0.f, 1.f, 0.f)));
  m_imageRegion = float4(0.f, 0.f, 1.f, 1.f);
  getParam("imageRegion", ANARI_FLOAT32_BOX2, &m_imageRegion);
  if (hasParam("aspect", ANARI_FLOAT32))
    m_aspect = getParam<float>("aspect", 1.f);
  else
    m_aspect.reset();
  m_apertureRadius = getParam<float>("apertureRadius", 0.f);
  m_focusDistance = getParam<float>("focusDistance", 1.f);
  if (m_apertureRadius < 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "camera 'apertureRadius' %f is negative, using 0",
        m_apertureRadius);
    m_apertureRadius = 0.f;
  }
  if (m_apertureRadius > 0.f && !(m_focusDistance > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "camera 'focusDistance' %f is not positive, ignoring 'apertureRadius'",
        m_focusDistance);
    m_apertureRadius = 0.f;
  }
}

void Camera::finalize()
{
  ispc::Camera_setConfig(getIE(),
      &m_pos.x,
      &m_dir.x,
      &m_up.x,
      &m_imageRegion.x,
      m_aspect.has_value(),
      m_aspect.value_or(1.f),
      m_apertureRadius,
      m_focusDistance);
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Camera *);
