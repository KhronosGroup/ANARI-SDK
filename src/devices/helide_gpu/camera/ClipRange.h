// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <optional>

namespace helide_gpu {

// The near and far clip distances of a camera's projection (named so as to
// avoid Windows' 'near' and 'far' macros)
struct ClipRange
{
  float zNear;
  float zFar;
};

// The clip range of a camera's projection, fitted to what the frame draws, as
// VisGL does: [zMin, zMax] is the range of the distances along the camera's
// view direction of the corners of the bounds of everything drawn (zMin > zMax
// when nothing is). The range encloses that with a small margin, so the depth
// buffer's precision isn't spread over empty space; the application's near
// and far (both > 0, near < far, for a perspective camera; near >= 0 for an
// orthographic one), when set, narrow it.
//
// Without an application near, a perspective camera's near is kept at least
// 1/1000 of its far, for the depth buffer's precision: it clips the foreground
// of a world much larger than the view (a terrain seen from close to it).
//
// When nothing drawn is in front of the camera within the application's
// range, it is that range, completed with the defaults the device used before
// fitting (near 0.001, far 1e6; near 0 for an orthographic camera).
ClipRange fitClipRange(float zMin,
    float zMax,
    bool perspective,
    std::optional<float> appNear,
    std::optional<float> appFar);

} // namespace helide_gpu
