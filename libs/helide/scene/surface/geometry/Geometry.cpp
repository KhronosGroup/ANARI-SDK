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

// Helper functions ///////////////////////////////////////////////////////////

template <typename T>
static const T *typedOffset(const void *mem, uint32_t offset)
{
  return ((const T *)mem) + offset;
}

template <typename ELEMENT_T, int NUM_COMPONENTS, bool SRGB = false>
static float4 getAttributeArrayAt_ufixed(void *data, uint32_t offset)
{
  constexpr float m = std::numeric_limits<ELEMENT_T>::max();
  float4 retval(0.f, 0.f, 0.f, 1.f);
  switch (NUM_COMPONENTS) {
  case 4:
    retval.w = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 3) / m);
  case 3:
    retval.z = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 2) / m);
  case 2:
    retval.y = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 1) / m);
  case 1:
    retval.x = toneMap<SRGB>(
        *typedOffset<ELEMENT_T>(data, NUM_COMPONENTS * offset + 0) / m);
  default:
    break;
  }

  return retval;
}

// Geometry definitions ///////////////////////////////////////////////////////

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
  case ANARI_UFIXED8_R_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 1, true>(arr->begin(), i);
    break;
  case ANARI_UFIXED8_RA_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 2, true>(arr->begin(), i);
    break;
  case ANARI_UFIXED8_RGB_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 3, true>(arr->begin(), i);
    break;
  case ANARI_UFIXED8_RGBA_SRGB:
    retval = getAttributeArrayAt_ufixed<uint8_t, 4, true>(arr->begin(), i);
    break;
  case ANARI_UFIXED8:
    retval = getAttributeArrayAt_ufixed<uint8_t, 1>(arr->begin(), i);
    break;
  case ANARI_UFIXED8_VEC2:
    retval = getAttributeArrayAt_ufixed<uint8_t, 2>(arr->begin(), i);
    break;
  case ANARI_UFIXED8_VEC3:
    retval = getAttributeArrayAt_ufixed<uint8_t, 3>(arr->begin(), i);
    break;
  case ANARI_UFIXED8_VEC4:
    retval = getAttributeArrayAt_ufixed<uint8_t, 4>(arr->begin(), i);
    break;
  case ANARI_UFIXED16:
    retval = getAttributeArrayAt_ufixed<uint16_t, 1>(arr->begin(), i);
    break;
  case ANARI_UFIXED16_VEC2:
    retval = getAttributeArrayAt_ufixed<uint16_t, 2>(arr->begin(), i);
    break;
  case ANARI_UFIXED16_VEC3:
    retval = getAttributeArrayAt_ufixed<uint16_t, 3>(arr->begin(), i);
    break;
  case ANARI_UFIXED16_VEC4:
    retval = getAttributeArrayAt_ufixed<uint16_t, 4>(arr->begin(), i);
    break;
  case ANARI_UFIXED32:
    retval = getAttributeArrayAt_ufixed<uint32_t, 1>(arr->begin(), i);
    break;
  case ANARI_UFIXED32_VEC2:
    retval = getAttributeArrayAt_ufixed<uint32_t, 2>(arr->begin(), i);
    break;
  case ANARI_UFIXED32_VEC3:
    retval = getAttributeArrayAt_ufixed<uint32_t, 3>(arr->begin(), i);
    break;
  case ANARI_UFIXED32_VEC4:
    retval = getAttributeArrayAt_ufixed<uint32_t, 4>(arr->begin(), i);
    break;
  default:
    break;
  }

  return retval;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Geometry *);
