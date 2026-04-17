// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace helide_gpu {

Instance::Instance(HelideGPUDeviceGlobalState *s)
    : Object(ANARI_INSTANCE, s), m_xfmArray(this), m_idArray(this)
{}

void Instance::commitParameters()
{
  m_xfmArray = getParamObject<Array1D>("transform");
  m_xfm = getParam<mat4>("transform", mat4(1));
  m_idArray = getParamObject<Array1D>("id");
  m_id = getParam<uint32_t>("id", ~0u);
  m_group = getParamObject<Group>("group");
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
        anari::toString(m_xfmArray->elementType()),
        anari::toString(ANARI_FLOAT32_MAT4));
    m_xfmArray = {};
  }
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");
}

uint32_t Instance::id(size_t i) const
{
  return m_xfmArray && m_idArray ? *m_idArray->valueAt<uint32_t>(i) : m_id;
}

size_t Instance::numTransforms() const
{
  return m_xfmArray ? m_xfmArray->totalSize() : 1;
}

mat4 Instance::xfm(size_t i) const
{
  return m_xfmArray ? *m_xfmArray->valueAt<mat4>(i) : m_xfm;
}

bool Instance::xfmIsIdentity(size_t i) const
{
  return xfm(i) == mat4(1);
}

const Group *Instance::group() const
{
  return m_group.ptr;
}

Group *Instance::group()
{
  return m_group.ptr;
}

bool Instance::isValid() const
{
  return m_group;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::Instance *);
