// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "World.h"

namespace helide {

World::World(HelideGlobalState *s)
    : Object(ANARI_WORLD, s),
      m_zeroSurfaceData(this),
      m_zeroVolumeData(this),
      m_instanceData(this)
{
  m_zeroGroup = new Group(s);
  m_zeroInstance = new Instance(s);
  m_zeroInstance->setParamDirect("group", m_zeroGroup.ptr);

  // never any public ref to these objects
  m_zeroGroup->refDec(helium::RefType::PUBLIC);
  m_zeroInstance->refDec(helium::RefType::PUBLIC);
}

World::~World()
{
  cleanup();
}

bool World::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint64_t size, uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    if (flags & ANARI_WAIT)
      embreeSceneUpdate();
    if (!m_embreeScene)
      return false;

    auto bounds = getEmbreeSceneBounds(m_embreeScene);
    for (auto *i : instances()) {
      for (auto *v : i->group()->volumes()) {
        if (v->isValid())
          bounds.extend(v->bounds());
      }
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
  m_instanceData = getParamObject<ObjectArray>("instance");
}

void World::finalize()
{
  cleanup();

  const bool addZeroInstance = m_zeroSurfaceData || m_zeroVolumeData;
  if (addZeroInstance)
    reportMessage(ANARI_SEVERITY_DEBUG, "helide::World will add zero instance");

  if (m_zeroSurfaceData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "helide::World found %zu surfaces in zero instance",
        m_zeroSurfaceData->size());
    m_zeroGroup->setParamDirect("surface", getParamDirect("surface"));
  } else
    m_zeroGroup->removeParam("surface");

  if (m_zeroVolumeData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "helide::World found %zu volumes in zero instance",
        m_zeroVolumeData->size());
    m_zeroGroup->setParamDirect("volume", getParamDirect("volume"));
  } else
    m_zeroGroup->removeParam("volume");

  m_zeroInstance->setParam("id", getParam<uint32_t>("id", ~0u));

  m_zeroGroup->commitParameters();
  m_zeroInstance->commitParameters();
  m_zeroGroup->finalize();
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

  m_objectUpdates.lastTLSBuild = 0;
  m_objectUpdates.lastBLSReconstructCheck = 0;
  m_objectUpdates.lastBLSCommitCheck = 0;
}

const std::vector<Instance *> &World::instances() const
{
  return m_instances;
}

void World::intersectVolumes(VolumeRay &ray) const
{
  const auto &insts = instances();
  for (uint32_t i = 0; i < insts.size(); i++) {
    const auto *inst = insts[i];
    inst->group()->intersectVolumes(ray, inst->invXfm(i));
    if (ray.volume)
      ray.instID = i;
  }
}

RTCScene World::embreeScene() const
{
  return m_embreeScene;
}

void World::embreeSceneUpdate()
{
  rebuildBLSs();
  recommitBLSs();
  rebuildTLS();
}

void World::rebuildBLSs()
{
  const auto &state = *deviceState();
  if (state.objectUpdates.lastBLSReconstructSceneRequest
      < m_objectUpdates.lastBLSReconstructCheck) {
    return;
  }

  m_objectUpdates.lastTLSBuild = 0; // BLS changed, so need to build TLS
  reportMessage(ANARI_SEVERITY_DEBUG,
      "helide::World rebuilding %zu BLSs",
      m_instances.size());
  std::for_each(m_instances.begin(), m_instances.end(), [&](auto *inst) {
    inst->group()->embreeSceneConstruct();
  });

  m_objectUpdates.lastBLSReconstructCheck = helium::newTimeStamp();
  m_objectUpdates.lastBLSCommitCheck = helium::newTimeStamp();
}

void World::recommitBLSs()
{
  const auto &state = *deviceState();
  if (state.objectUpdates.lastBLSCommitSceneRequest
      < m_objectUpdates.lastBLSCommitCheck) {
    return;
  }

  m_objectUpdates.lastTLSBuild = 0; // BLS changed, so need to build TLS
  reportMessage(ANARI_SEVERITY_DEBUG,
      "helide::World recommitting %zu BLSs",
      m_instances.size());
  std::for_each(m_instances.begin(), m_instances.end(), [&](auto *inst) {
    inst->group()->embreeSceneCommit();
  });

  m_objectUpdates.lastBLSCommitCheck = helium::newTimeStamp();
}

void World::rebuildTLS()
{
  const auto &state = *deviceState();
  if (state.objectUpdates.lastTLSReconstructSceneRequest
      < m_objectUpdates.lastTLSBuild) {
    return;
  }

  reportMessage(ANARI_SEVERITY_DEBUG,
      "helide::World rebuilding TLS over %zu instances",
      m_instances.size());

  rtcReleaseScene(m_embreeScene);
  m_embreeScene = rtcNewScene(deviceState()->embreeDevice);

  uint32_t id = 0;
  std::for_each(m_instances.begin(), m_instances.end(), [&](auto *i) {
    if (i && i->isValid() && !i->group()->surfaces().empty()) {
      i->embreeGeometryUpdate();
      rtcAttachGeometryByID(m_embreeScene, i->embreeGeometry(), id);
    } else {
      if (i->group()->surfaces().empty()) {
        reportMessage(ANARI_SEVERITY_DEBUG,
            "helide::World rejecting empty surfaces in instance(%p) "
            "when building TLS",
            i);
      } else {
        reportMessage(ANARI_SEVERITY_DEBUG,
            "helide::World rejecting invalid surfaces in instance(%p) "
            "when building TLS",
            i);
      }
    }
    id++;
  });

  rtcCommitScene(m_embreeScene);
  m_objectUpdates.lastTLSBuild = helium::newTimeStamp();
}

void World::cleanup()
{
  rtcReleaseScene(m_embreeScene);
  m_embreeScene = nullptr;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::World *);
