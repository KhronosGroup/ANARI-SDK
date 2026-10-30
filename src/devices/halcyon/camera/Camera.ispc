// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "shading/halcyon_objects.isph"
#include "shading/halcyon_util.isph"

// Exported interface /////////////////////////////////////////////////////////

export uniform ulong sizeof_Camera()
{
  return sizeof(uniform Camera);
}

// 'dir' and 'up' are normalized; 'aspect' is ignored unless 'hasAspect';
// 'focusDistance' is positive unless 'apertureRadius' is 0
export void Camera_setConfig(void *uniform _self,
    uniform float *uniform pos /* float3 */,
    uniform float *uniform dir /* float3 */,
    uniform float *uniform up /* float3 */,
    uniform float *uniform region /* float4 */,
    uniform bool hasAspect,
    uniform float aspect,
    uniform float apertureRadius,
    uniform float focusDistance)
{
  uniform Camera *uniform self = object_cast<Camera>(_self);
  self->pos = load_float3(pos);
  self->dir = load_float3(dir);
  self->up = load_float3(up);
  self->region = load_float4(region);
  self->hasAspect = hasAspect;
  self->aspect = aspect;
  self->apertureRadius = apertureRadius;
  self->focusDistance = focusDistance;
  self->imagePlaneU = normalize(cross(self->dir, self->up));
  self->imagePlaneV = normalize(cross(self->imagePlaneU, self->dir));
}

export void Camera_setPerspectiveHeight(
    void *uniform _self, uniform float imagePlaneHeight)
{
  uniform Camera *uniform self = object_cast<Camera>(_self);
  self->imagePlaneHeight = imagePlaneHeight;
  self->type = CAMERA_PERSPECTIVE;
}

export void Camera_setOrthographicHeight(
    void *uniform _self, uniform float imagePlaneHeight)
{
  uniform Camera *uniform self = object_cast<Camera>(_self);
  self->imagePlaneHeight = imagePlaneHeight;
  self->type = CAMERA_ORTHOGRAPHIC;
}
