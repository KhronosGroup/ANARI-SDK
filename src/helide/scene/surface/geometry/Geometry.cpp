// Copyright 2022-2024 The Khronos Group
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

Geometry::Geometry(HelideGlobalState *s) : Object(ANARI_GEOMETRY, s) {}

Geometry::~Geometry()
{
  rtcReleaseGeometry(m_embreeGeometry);
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
  m_uniformAttr[0] = getParam<float4>("attribute0", DEFAULT_ATTRIBUTE_VALUE);
  m_uniformAttr[1] = getParam<float4>("attribute1", DEFAULT_ATTRIBUTE_VALUE);
  m_uniformAttr[2] = getParam<float4>("attribute2", DEFAULT_ATTRIBUTE_VALUE);
  m_uniformAttr[3] = getParam<float4>("attribute3", DEFAULT_ATTRIBUTE_VALUE);
  m_uniformAttr[4] = getParam<float4>("color", DEFAULT_ATTRIBUTE_VALUE);
  m_primitiveAttr[0] = getParamObject<Array1D>("primitive.attribute0");
  m_primitiveAttr[1] = getParamObject<Array1D>("primitive.attribute1");
  m_primitiveAttr[2] = getParamObject<Array1D>("primitive.attribute2");
  m_primitiveAttr[3] = getParamObject<Array1D>("primitive.attribute3");
  m_primitiveAttr[4] = getParamObject<Array1D>("primitive.color");
}

void Geometry::markCommitted()
{
  Object::markCommitted();
  deviceState()->objectUpdates.lastBLSCommitSceneRequest =
      helium::newTimeStamp();
}

float4 Geometry::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (attr == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  auto attrIdx = static_cast<int>(attr);
  return readAttributeValue(
      m_primitiveAttr[attrIdx].ptr, ray.primID, m_uniformAttr[attrIdx]);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Geometry *);
