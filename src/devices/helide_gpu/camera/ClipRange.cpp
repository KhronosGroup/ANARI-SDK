// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ClipRange.h"
// std
#include <algorithm>

namespace helide_gpu {

// the margin around the drawn bounds, relative to their far distance
constexpr float MARGIN = 1e-3f;
// a perspective near's smallest fraction of the far without an application near
constexpr float MIN_NEAR_OVER_FAR = 1e-3f;
constexpr float DEFAULT_NEAR = 1e-3f;
constexpr float DEFAULT_FAR = 1e6f;

ClipRange fitClipRange(float zMin,
    float zMax,
    bool perspective,
    std::optional<float> appNear,
    std::optional<float> appFar)
{
  // something drawn is in front of the camera (false for empty bounds, bounds
  // behind it, and NaNs)
  if (zMin <= zMax && zMax > 0.f) {
    ClipRange r;
    r.zFar = zMax * (1.f + MARGIN);
    if (perspective) {
      r.zNear = appNear
          ? std::max(*appNear, zMin * (1.f - MARGIN))
          : std::max(r.zFar * MIN_NEAR_OVER_FAR, zMin * (1.f - MARGIN));
    } else {
      r.zNear = std::max(appNear.value_or(0.f), zMin - MARGIN * zMax);
    }
    if (appFar)
      r.zFar = std::min(r.zFar, *appFar);
    if (r.zFar > r.zNear)
      return r;
  }

  // nothing drawn is within the application's range, so nothing is visible:
  // any valid projection will do
  ClipRange r;
  if (appNear)
    r.zNear = *appNear;
  else if (!perspective)
    r.zNear = 0.f;
  else
    r.zNear = appFar ? *appFar * MIN_NEAR_OVER_FAR : DEFAULT_NEAR;
  r.zFar = appFar.value_or(std::max(DEFAULT_FAR, 2.f * r.zNear));
  return r;
}

} // namespace helide_gpu
