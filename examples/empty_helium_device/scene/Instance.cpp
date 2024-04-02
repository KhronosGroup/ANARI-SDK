// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace hecore {

Instance::Instance(HeCoreDeviceGlobalState *s) : Object(ANARI_INSTANCE, s) {}

void Instance::commit()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_xfm = getParam<mat4>("transform", mat4(linalg::identity));
  m_xfmInvRot = linalg::inverse(extractRotation(m_xfm));
  m_group = getParamObject<Group>("group");
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

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Instance *);
