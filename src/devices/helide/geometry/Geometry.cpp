// Copyright 2021-2026 The Khronos Group
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
#include <algorithm>
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
}

void Geometry::markFinalized()
{
  Object::markFinalized();
  deviceState()->objectUpdates.lastBLSCommitSceneRequest =
      helium::newTimeStamp();
}

float4 Geometry::getAttributeValue(const Attribute &attr, const Ray &ray) const
{
  if (auto a = getRayAttribute(attr, ray); a.has_value())
    return *a;

  const auto attrIdx = static_cast<int>(attr);
  return readAttributeValue(m_primitiveAttr[attrIdx].ptr,
      ray.primID,
      m_uniformAttr[attrIdx].value_or(DEFAULT_ATTRIBUTE_VALUE));
}

Geometry::Radii Geometry::readRadii(
    const Array1D *array, const char *name, float fallback, size_t needed) const
{
  Radii radii;
  radii.values = array ? array->beginAs<float>() : nullptr;
  radii.count = array ? array->size() : 0;
  radii.fallback = fallback;
  if (array && radii.count < needed) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'%s' has %zu elements for %zu vertices; the rest use 'radius'",
        name,
        radii.count,
        needed);
  }
  return radii;
}

bool Geometry::readIndices(
    const Array1D &index, const char *subtype, std::vector<uint32_t> &out) const
{
  const auto type = index.elementType();
  if (type == ANARI_UINT32) {
    out.assign(index.beginAs<uint32_t>(), index.endAs<uint32_t>());
    return true;
  } else if (type != ANARI_UINT64) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "%s 'primitive.index' array elements are %s, but need to be "
        "ANARI_UINT32 or ANARI_UINT64; the %s is left empty",
        subtype,
        anari::toString(type),
        subtype);
    return false;
  }

  bool truncated = false;
  out.resize(index.size());
  std::transform(index.beginAs<uint64_t>(),
      index.endAs<uint64_t>(),
      out.begin(),
      [&](uint64_t v) {
        truncated |= v > std::numeric_limits<uint32_t>::max();
        return uint32_t(v);
      });
  if (truncated) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "%s 'primitive.index' values above UINT32_MAX are truncated to 32 bits",
        subtype);
  }
  return true;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Geometry *);
