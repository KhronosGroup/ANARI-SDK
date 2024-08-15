// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace helide {

Instance::Instance(HelideGlobalState *s) : Object(ANARI_INSTANCE, s)
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_INSTANCE);
}

Instance::~Instance()
{
  rtcReleaseGeometry(m_embreeGeometry);
}

void Instance::commit()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_xfm = getParam<mat4>("transform", mat4(linalg::identity));
  m_xfmInvRot = linalg::inverse(extractRotation(m_xfm));
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
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");
}

uint32_t Instance::id() const
{
  return m_id;
}

const mat4 &Instance::xfm() const
{
  return m_xfm;
}

const mat3 &Instance::xfmInvRot() const
{
  return m_xfmInvRot;
}

bool Instance::xfmIsIdentity() const
{
  return xfm() == mat4(linalg::identity);
}

const UniformAttributeSet &Instance::getUniformAttributes() const
{
  return m_uniformAttr;
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
  rtcSetGeometryTransform(
      m_embreeGeometry, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, &m_xfm);
  rtcCommitGeometry(m_embreeGeometry);
}

void Instance::markCommitted()
{
  Object::markCommitted();
  deviceState()->objectUpdates.lastTLSReconstructSceneRequest =
      helium::newTimeStamp();
}

bool Instance::isValid() const
{
  return m_group;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Instance *);
