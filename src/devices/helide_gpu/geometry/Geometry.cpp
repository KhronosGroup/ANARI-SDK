// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// subtypes
#include "Cone.h"
#include "Curve.h"
#include "Cylinder.h"
#include "Sphere.h"
#include "Triangle.h"

namespace helide_gpu {

Geometry::Geometry(HelideGPUDeviceGlobalState *s) : Object(ANARI_GEOMETRY, s) {}

box3 Geometry::bounds() const
{
  return {};
}

Geometry *Geometry::createInstance(
    std::string_view subtype, HelideGPUDeviceGlobalState *s)
{
  if (subtype == "triangle")
    return (Geometry *)new Triangle(s);
  if (subtype == "curve")
    return (Geometry *)new Curve(s);
  if (subtype == "cone")
    return (Geometry *)new Cone(s);
  if (subtype == "cylinder")
    return (Geometry *)new Cylinder(s);
  if (subtype == "sphere")
    return (Geometry *)new Sphere(s);
  return (Geometry *)new UnknownObject(ANARI_GEOMETRY, s);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Geometry *);
