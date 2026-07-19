// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "World.h"
#include "volume/Volume.h"
// std
#include <cstring>

namespace helide_gpu {

World::World(HelideGPUDeviceGlobalState *s)
    : Object(ANARI_WORLD, s),
      m_zeroSurfaceData(this),
      m_zeroVolumeData(this),
      m_zeroLightData(this),
      m_instanceData(this)
{
  m_zeroGroup = new Group(s);
  m_zeroInstance = new Instance(s);
  m_zeroInstance->setParamDirect("group", m_zeroGroup.ptr);

  // never any public ref to these objects
  m_zeroGroup->refDec(helium::RefType::PUBLIC);
  m_zeroInstance->refDec(helium::RefType::PUBLIC);
}

World::~World() = default;

bool World::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    if (flags & ANARI_WAIT) {
        deviceState()->commitBuffer.flush();
        // Ensure all pending GPU work is completed before reading the
        // bounds property, which may depend on geometry that is uploaded
        // asynchronously.  A no‑op task is enqueued and we wait on its
        // future to block until the GPU thread is idle.
        deviceState()->gpu.thread.flush();
    }

    box3 bounds{};
    for (auto *inst : m_instances) {
      if (!inst || !inst->isValid())
        continue;
      const auto *grp = inst->group();
      if (!grp)
        continue;

      box3 groupBounds;
      for (auto *surf : grp->surfaces()) {
        if (!surf || !surf->isValid())
          continue;
        const auto *geom = surf->geometry();
        if (geom)
          groupBounds.extend(geom->bounds());
      }

      for (auto *vol : grp->volumes()) {
        if (!vol || !vol->isValid())
          continue;
        groupBounds.extend(vol->bounds());
      }

      if (!inst->xfmIsIdentity())
        groupBounds = xfmBox(inst->xfm(), groupBounds);
      bounds.extend(groupBounds);
    }

    std::memcpy(ptr, &bounds, sizeof(bounds));
    return true;
  }

  return Object::getProperty(name, type, ptr, size, flags);
}

void World::commitParameters()
{
  m_zeroSurfaceData = getParamObject<ObjectArray>("surface");
  m_zeroVolumeData = getParamObject<ObjectArray>("volume");
  m_zeroLightData = getParamObject<ObjectArray>("light");
  m_instanceData = getParamObject<ObjectArray>("instance");
}

void World::finalize()
{
  const bool addZeroInstance =
      m_zeroSurfaceData || m_zeroVolumeData || m_zeroLightData;
  if (addZeroInstance)
    reportMessage(ANARI_SEVERITY_DEBUG, "helide_gpu::World will add zero instance");

  if (m_zeroSurfaceData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "helide_gpu::World found %zu surfaces in zero instance",
        m_zeroSurfaceData->size());
    m_zeroGroup->setParamDirect("surface", getParamDirect("surface"));
  } else
    m_zeroGroup->removeParam("surface");

  if (m_zeroVolumeData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "helide_gpu::World found %zu volumes in zero instance",
        m_zeroVolumeData->size());
    m_zeroGroup->setParamDirect("volume", getParamDirect("volume"));
  } else
    m_zeroGroup->removeParam("volume");

  if (m_zeroLightData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "helide_gpu::World found %zu lights in zero instance",
        m_zeroLightData->size());
    m_zeroGroup->setParamDirect("light", getParamDirect("light"));
  } else
    m_zeroGroup->removeParam("light");

  m_zeroInstance->setParam("id", getParam<uint32_t>("id", ~0u));

  m_zeroGroup->commitParameters();
  m_zeroGroup->finalize();
  m_zeroInstance->commitParameters();
  m_zeroInstance->finalize();

  m_instances.clear();

  if (m_instanceData) {
    std::for_each(m_instanceData->handlesBegin(),
        m_instanceData->handlesEnd(),
        [&](auto *o) {
          if (o && o->isValid())
            m_instances.push_back((Instance *)o);
        });
  }

  if (addZeroInstance)
    m_instances.push_back(m_zeroInstance.ptr);
}

const std::vector<Instance *> &World::instances() const
{
  return m_instances;
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_DEFINITION(helide_gpu::World *);
