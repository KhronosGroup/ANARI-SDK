// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "World.h"

namespace anari {
namespace example_device {

World::World()
{
  setCommitPriority(COMMIT_PRIORITY_WORLD);

  m_zeroGroup = new Group;
  m_zeroInstance = new Instance;
  m_zeroInstance->setParamDirect("group", m_zeroGroup);

  m_zeroGroup->refDec(RefType::PUBLIC); // never any public ref to this obj
  m_zeroInstance->refDec(RefType::PUBLIC); // never any public ref to this obj
}

void World::commit()
{
  m_instances.reset();

  bool addZeroInstance = hasParam("surface") || hasParam("volume");

  if (hasParam("surface"))
    m_zeroGroup->setParamDirect("surface", getParamDirect("surface"));
  else
    m_zeroGroup->removeParam("surface");

  if (hasParam("volume"))
    m_zeroGroup->setParamDirect("volume", getParamDirect("volume"));
  else
    m_zeroGroup->removeParam("volume");

  if (addZeroInstance) {
    m_zeroGroup->commit();
    m_zeroInstance->commit();
  }

  m_instanceData = getParamObject<ObjectArray>("instance");
  if (m_instanceData) {
    if (addZeroInstance) {
      m_instanceData->removeAppendedHandles();
      m_instanceData->appendHandle(m_zeroInstance.ptr);
    }
    m_instances = make_Span(
        (Instance **)m_instanceData->handles(), m_instanceData->size());
  } else
    m_instances = make_Span((Instance **)&m_zeroInstance.ptr, 1);

  buildBVH((prim_t *)m_instances.data(), m_instances.size());
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::World *);

} // namespace anari
