// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
// std
#include <iterator>

namespace hecore {

Group::Group(HeCoreDeviceGlobalState *s)
    : Object(ANARI_GROUP, s),
      m_surfaceData(this),
      m_volumeData(this),
      m_lightData(this)
{}

Group::~Group() = default;

bool Group::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  return Object::getProperty(name, type, ptr, flags);
}

void Group::commitParameters()
{
  m_surfaceData = getParamObject<ObjectArray>("surface");
  m_volumeData = getParamObject<ObjectArray>("volume");
  m_lightData = getParamObject<ObjectArray>("light");
}

void Group::finalize()
{
  m_surfaces.clear();
  m_volumes.clear();
  m_lights.clear();

  if (m_surfaceData) {
    std::transform(m_surfaceData->handlesBegin(),
        m_surfaceData->handlesEnd(),
        std::back_inserter(m_surfaces),
        [](auto *o) { return (Surface *)o; });
  }

  if (m_volumeData) {
    std::transform(m_volumeData->handlesBegin(),
        m_volumeData->handlesEnd(),
        std::back_inserter(m_volumes),
        [](auto *o) { return (Volume *)o; });
  }

  if (m_lightData) {
    std::transform(m_lightData->handlesBegin(),
        m_lightData->handlesEnd(),
        std::back_inserter(m_lights),
        [](auto *o) { return (Light *)o; });
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

const std::vector<Light *> &Group::lights() const
{
  return m_lights;
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Group *);
