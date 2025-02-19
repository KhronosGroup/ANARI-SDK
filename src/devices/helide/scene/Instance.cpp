// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace helide {

Instance::Instance(HelideGlobalState *s)
    : Object(ANARI_INSTANCE, s), m_xfmArray(this), m_idArray(this)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_INSTANCE_ARRAY);
}

Instance::~Instance()
{
  rtcReleaseGeometry(m_embreeGeometry);
}

void Instance::commitParameters()
{
  m_idArray = getParamObject<Array1D>("id");
  m_id = getParam<uint32_t>("id", ~0u);
  m_xfmArray = getParamObject<Array1D>("transform");
  m_xfm = getParam<mat4>("transform", mat4(linalg::identity));
  m_group = getParamObject<Group>("group");

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

  m_uniformAttrArrays.attribute0 = getParamObject<Array1D>("attribute0");
  m_uniformAttrArrays.attribute1 = getParamObject<Array1D>("attribute1");
  m_uniformAttrArrays.attribute2 = getParamObject<Array1D>("attribute2");
  m_uniformAttrArrays.attribute3 = getParamObject<Array1D>("attribute3");
  m_uniformAttrArrays.color = getParamObject<Array1D>("color");
}

void Instance::finalize()
{
  if (m_idArray && m_idArray->elementType() != ANARI_UINT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'id' array elements are %s, but need to be %s",
        anari::toString(m_idArray->elementType()),
        anari::toString(ANARI_UINT32));
    m_idArray = {};
  }
  if (m_xfmArray && m_xfmArray->elementType() != ANARI_FLOAT32_MAT4) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'transform' array elements are %s, but need to be %s",
        anari::toString(m_idArray->elementType()),
        anari::toString(ANARI_FLOAT32_MAT4));
    m_xfmArray = {};
  }
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");
}

void Instance::markFinalized()
{
  Object::markFinalized();
  deviceState()->objectUpdates.lastTLSReconstructSceneRequest =
      helium::newTimeStamp();
}

bool Instance::isValid() const
{
  return m_group;
}


uint32_t Instance::numTransforms() const
{
  return m_xfmArray ? uint32_t(m_xfmArray->totalSize()) : 1u;
}

uint32_t Instance::id(uint32_t i) const
{
  return m_xfmArray && m_idArray ? *m_idArray->valueAt<uint32_t>(i) : m_id;
}

const mat4 &Instance::xfm(uint32_t i) const
{
  return m_xfmArray ? *m_xfmArray->valueAt<mat4>(i) : m_xfm;
}

UniformAttributeSet Instance::getUniformAttributes(uint32_t i) const
{
  UniformAttributeSet retval = m_uniformAttr;

  if (m_uniformAttrArrays.attribute0)
    retval[0] = m_uniformAttrArrays.attribute0->readAsAttributeValue(i);
  if (m_uniformAttrArrays.attribute1)
    retval[1] = m_uniformAttrArrays.attribute1->readAsAttributeValue(i);
  if (m_uniformAttrArrays.attribute2)
    retval[2] = m_uniformAttrArrays.attribute2->readAsAttributeValue(i);
  if (m_uniformAttrArrays.attribute3)
    retval[3] = m_uniformAttrArrays.attribute3->readAsAttributeValue(i);
  if (m_uniformAttrArrays.color)
    retval[4] = m_uniformAttrArrays.color->readAsAttributeValue(i);

  return retval;
}

const Group *Instance::group() const
{
  return m_group.ptr;
}

Group *Instance::group()
{
  return m_group.ptr;
}

RTCGeometry Instance::embreeGeometry() const
{
  return m_embreeGeometry;
}

void Instance::embreeGeometryUpdate()
{
  rtcSetGeometryInstancedScene(m_embreeGeometry, group()->embreeScene());
  auto *xfms = rtcSetNewGeometryBuffer(m_embreeGeometry,
      RTC_BUFFER_TYPE_TRANSFORM,
      0,
      RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
      sizeof(mat4),
      this->numTransforms());
  std::memcpy(xfms,
      m_xfmArray ? m_xfmArray->begin() : &m_xfm,
      this->numTransforms() * sizeof(mat4));
  rtcCommitGeometry(m_embreeGeometry);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Instance *);
