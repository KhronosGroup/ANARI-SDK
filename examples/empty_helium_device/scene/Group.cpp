// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
// std
#include <iterator>

namespace hecore {

Group::Group(HeCoreDeviceGlobalState *s) : Object(ANARI_GROUP, s) {}

Group::~Group()
{
  cleanup();
}

bool Group::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  return Object::getProperty(name, type, ptr, flags);
}

void Group::commit()
{
  cleanup();

  m_surfaceData = getParamObject<ObjectArray>("surface");
  m_volumeData = getParamObject<ObjectArray>("volume");

  if (m_surfaceData) {
    m_surfaceData->addCommitObserver(this);
    std::transform(m_surfaceData->handlesBegin(),
        m_surfaceData->handlesEnd(),
        std::back_inserter(m_surfaces),
        [](auto *o) { return (Surface *)o; });
  }

  if (m_volumeData) {
    m_volumeData->addCommitObserver(this);
    std::transform(m_volumeData->handlesBegin(),
        m_volumeData->handlesEnd(),
        std::back_inserter(m_volumes),
        [](auto *o) { return (Volume *)o; });
  }
}

const std::vector<Surface *> &Group::surfaces() const
{
  return m_surfaces;
}

const std::vector<Volume *> &Group::volumes() const
{
  return m_volumes;
}

void Group::cleanup()
{
  if (m_surfaceData)
    m_surfaceData->removeCommitObserver(this);
  if (m_volumeData)
    m_volumeData->removeCommitObserver(this);

  m_surfaces.clear();
  m_volumes.clear();
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Group *);
