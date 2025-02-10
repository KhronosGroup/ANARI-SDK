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

void Geometry::commitParameters()
{
  for (auto &a : m_uniformAttr)
    a.reset();
  float4 attrV = DEFAULT_ATTRIBUTE_VALUE;
  if (getParam("attribute0", ANARI_FLOAT32_VEC4, &attrV))
    m_uniformAttr[0] = attrV;
  if (getParam("attribute1", ANARI_FLOAT32_VEC4, &attrV))
    m_uniformAttr[1] = attrV;
  if (getParam("attribute2", ANARI_FLOAT32_VEC4, &attrV))
    m_uniformAttr[2] = attrV;
  if (getParam("attribute3", ANARI_FLOAT32_VEC4, &attrV))
    m_uniformAttr[3] = attrV;
  if (getParam("color", ANARI_FLOAT32_VEC4, &attrV))
    m_uniformAttr[4] = attrV;
  m_primitiveAttr[0] = getParamObject<Array1D>("primitive.attribute0");
  m_primitiveAttr[1] = getParamObject<Array1D>("primitive.attribute1");
  m_primitiveAttr[2] = getParamObject<Array1D>("primitive.attribute2");
  m_primitiveAttr[3] = getParamObject<Array1D>("primitive.attribute3");
  m_primitiveAttr[4] = getParamObject<Array1D>("primitive.color");
  m_primitiveId = getParamObject<Array1D>("primitive.id");
  if (m_primitiveId
      && !(m_primitiveId->elementType() != ANARI_UINT32
          || m_primitiveId->elementType() != ANARI_UINT64)) {
    m_primitiveId = nullptr;
  }
}

void Geometry::markFinalized()
{
  Object::markFinalized();
  deviceState()->objectUpdates.lastBLSCommitSceneRequest =
      helium::newTimeStamp();
}

float4 Geometry::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (attr == Attribute::NONE)
    return DEFAULT_ATTRIBUTE_VALUE;

  const auto attrIdx = static_cast<int>(attr);
  return readAttributeValue(m_primitiveAttr[attrIdx].ptr,
      ray.primID,
      m_uniformAttr[attrIdx].value_or(DEFAULT_ATTRIBUTE_VALUE));
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Geometry *);
