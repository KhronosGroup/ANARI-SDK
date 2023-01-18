// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Geometry.h"
// subtypes
#include "Cone.h"
#include "Curve.h"
#include "Cylinder.h"
#include "Quad.h"
#include "Sphere.h"
#include "Triangle.h"
// std
#include <cstring>
#include <limits>

namespace helide {

Geometry::Geometry(HelideGlobalState *s) : Object(ANARI_GEOMETRY, s)
{
  s->objectCounts.geometries++;
}

Geometry::~Geometry()
{
  rtcReleaseGeometry(m_embreeGeometry);
  deviceState()->objectCounts.geometries--;
}

Geometry *Geometry::createInstance(
    std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "cone")
    return new Cone(s);
  else if (subtype == "curve")
    return new Curve(s);
  else if (subtype == "cylinder")
    return new Cylinder(s);
  else if (subtype == "quad")
    return new Quad(s);
  else if (subtype == "sphere")
    return new Sphere(s);
  else if (subtype == "triangle")
    return new Triangle(s);
  else
    return (Geometry *)new UnknownObject(ANARI_GEOMETRY, s);
}

RTCGeometry Geometry::embreeGeometry() const
{
  return m_embreeGeometry;
}

void Geometry::commit()
{
  m_attributes[0] = getParamObject<Array1D>("primitive.attribute0");
  m_attributes[1] = getParamObject<Array1D>("primitive.attribute1");
  m_attributes[2] = getParamObject<Array1D>("primitive.attribute2");
  m_attributes[3] = getParamObject<Array1D>("primitive.attribute3");
  m_attributes[4] = getParamObject<Array1D>("primitive.color");
}

void Geometry::markCommitted()
{
  Object::markCommitted();
  deviceState()->objectUpdates.lastBLSCommitSceneRequest =
      helium::newTimeStamp();
}

float4 Geometry::getAttributeValueAt(
    const Attribute &attr, const Ray &ray) const
{
  if (attr == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  auto attrIdx = static_cast<int>(attr);
  return readAttributeArrayAt(m_attributes[attrIdx].ptr, ray.primID);
}

float4 Geometry::readAttributeArrayAt(Array1D *arr, uint32_t i) const
{
  auto retval = DEFAULT_ATTRIBUTE_VALUE;

  if (!arr)
    return retval;

  switch (arr->elementType()) {
  case ANARI_FLOAT32:
    std::memcpy(&retval, arr->beginAs<float>() + i, sizeof(float));
    break;
  case ANARI_FLOAT32_VEC2:
    std::memcpy(&retval, arr->beginAs<float2>() + i, sizeof(float2));
    break;
  case ANARI_FLOAT32_VEC3:
    std::memcpy(&retval, arr->beginAs<float3>() + i, sizeof(float3));
    break;
  case ANARI_FLOAT32_VEC4:
    std::memcpy(&retval, arr->beginAs<float4>() + i, sizeof(float4));
    break;
  /////////////////////////////////////////////////////////////////////////////
  // TODO: add cases for other color types (fixed8/16/32, SRGB)
  /////////////////////////////////////////////////////////////////////////////
  default:
    break;
  }

  return retval;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Geometry *);
