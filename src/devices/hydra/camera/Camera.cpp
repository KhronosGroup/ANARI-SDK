// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Camera.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "Orthographic.h"
#include "Perspective.h"

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/vec3d.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

Camera::Camera(HydraGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera *Camera::createInstance(std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "perspective")
    return new Perspective(s);
  else if (subtype == "orthographic")
    return new Orthographic(s);
  else
    return (Camera *)new UnknownObject(ANARI_CAMERA, s);
}

void Camera::commitParameters()
{
  m_position = getParam<helium::float3>("position", helium::float3(0, 0, 0));
  m_direction = getParam<helium::float3>("direction", helium::float3(0, 0, -1));
  m_up = getParam<helium::float3>("up", helium::float3(0, 1, 0));
  m_aspect = getParam<float>("aspect", 0.f);
  m_clippingRange =
      getParam<helium::float2>("hydra.clippingRange", helium::float2(0.f));
  if ((m_clippingRange.x != 0 || m_clippingRange.y != 0)
      && (!std::isfinite(m_clippingRange.x) || !std::isfinite(m_clippingRange.y)
          || m_clippingRange.x <= 0
          || m_clippingRange.y <= m_clippingRange.x)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "hydra.clippingRange requires finite 0 < near < far; using automatic clipping");
    m_clippingRange = helium::float2(0.f);
  }
  m_imageRegion = helium::float4(0, 0, 1, 1);
  getParam("imageRegion", ANARI_FLOAT32_BOX2, &m_imageRegion);
}

void Camera::finalize()
{
  // Camera state is consumed per-frame; nothing to author into the mirror.
}

GfMatrix4d Camera::viewMatrix() const
{
  const GfVec3d eye(m_position.x, m_position.y, m_position.z);
  const GfVec3d dir(m_direction.x, m_direction.y, m_direction.z);
  const GfVec3d up(m_up.x, m_up.y, m_up.z);
  GfMatrix4d view;
  view.SetLookAt(eye, eye + dir, up);
  return view;
}

// Perspective ////////////////////////////////////////////////////////////////

Perspective::Perspective(HydraGlobalState *s) : Camera(s) {}

void Perspective::commitParameters()
{
  Camera::commitParameters();
  m_fovy = getParam<float>("fovy", 1.0471975512f);
}

double Perspective::imageHeight() const
{
  return 2.0 * std::tan(m_fovy * 0.5);
}

void Orthographic::commitParameters()
{
  Camera::commitParameters();
  m_height = getParam<float>("height", 1.f);
}

GfMatrix4d Camera::projectionMatrix(
    double frameAspect, const Bounds &bounds) const
{
  const double halfHeight = imageHeight() * 0.5;
  const double halfWidth = halfHeight * (m_aspect > 0 ? m_aspect : frameAspect);
  // Perspective windows are measured at unit distance; orthographic windows
  // are measured in world units. Regions map the selected full-image window
  // onto the entire frame without any additional aspect correction.
  GfFrustum frustum;
  frustum.SetProjectionType(
      isOrthographic() ? GfFrustum::Orthographic : GfFrustum::Perspective);
  // Fit committed bounds in camera space each render. Padding keeps planar
  // geometry away from the far-plane depth clear value.
  GfRange1d clipping(0.01, 1000.0);
  if (!bounds.IsEmpty()) {
    const auto view = viewMatrix();
    double nearest = std::numeric_limits<double>::infinity();
    double farthest = -nearest;
    for (int i = 0; i < 8; ++i) {
      const double distance = -view.Transform(GfVec3d(bounds.GetCorner(i)))[2];
      nearest = std::min(nearest, distance);
      farthest = std::max(farthest, distance);
    }
    if (std::isfinite(nearest) && std::isfinite(farthest) && farthest > 0) {
      const double far = farthest * 1.01;
      // An eye-crossing box has no positive nearest bound. Limit the depth
      // ratio there; callers can override it for very close visible content.
      const double near = nearest > 0 ? nearest * 0.99 : far * 1e-4;
      clipping = GfRange1d(near, far);
    }
  }
  if (m_clippingRange.x > 0)
    clipping = GfRange1d(m_clippingRange.x, m_clippingRange.y);
  frustum.SetNearFar(clipping);
  frustum.SetWindow(GfRange2d(GfVec2d((2 * m_imageRegion.x - 1) * halfWidth,
                                  (2 * m_imageRegion.y - 1) * halfHeight),
      GfVec2d((2 * m_imageRegion.z - 1) * halfWidth,
          (2 * m_imageRegion.w - 1) * halfHeight)));
  return frustum.ComputeProjectionMatrix();
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Camera *);
}
