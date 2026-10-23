// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "World.h"
#include "Light_ispc.h"
#include "World_ispc.h"

namespace halcyon {

// Bounds of 'b' (not empty) transformed by 'xfm': the box around its corners
static box3 xfmBox(const mat4 &xfm, const box3 &b)
{
  box3 result;
  for (int c = 0; c < 8; c++) {
    const float3 p((c & 1) ? b.upper.x : b.lower.x,
        (c & 2) ? b.upper.y : b.lower.y,
        (c & 4) ? b.upper.z : b.lower.z);
    result.extend(linalg::mul(xfm, float4(p, 1.f)).xyz());
  }
  return result;
}

World::World(HalcyonDeviceGlobalState *s)
    : Object(ANARI_WORLD, s, ispc::sizeof_World()),
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

  ispc::World_setVolumeInstances(getIE(), nullptr, 0);
  ispc::World_setLights(getIE(), nullptr, 0);
}

World::~World()
{
  cleanup();
}

bool World::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    // HalcyonDevice runs ANARI_WAIT queries on the task queue, so this
    // doesn't update the scene while a frame renders it
    if (flags & ANARI_WAIT)
      embreeSceneUpdate();
    const auto b = bounds();
    if (!b)
      return false;
    std::memcpy(ptr, &*b, sizeof(box3));
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
  cleanup();

  const bool addZeroInstance =
      m_zeroSurfaceData || m_zeroVolumeData || m_zeroLightData;
  if (addZeroInstance)
    reportMessage(
        ANARI_SEVERITY_DEBUG, "halcyon::World will add zero instance");

  if (m_zeroSurfaceData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::World found %zu surfaces in zero instance",
        m_zeroSurfaceData->size());
    m_zeroGroup->setParamDirect("surface", getParamDirect("surface"));
  } else
    m_zeroGroup->removeParam("surface");

  if (m_zeroVolumeData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::World found %zu volumes in zero instance",
        m_zeroVolumeData->size());
    m_zeroGroup->setParamDirect("volume", getParamDirect("volume"));
  } else
    m_zeroGroup->removeParam("volume");

  if (m_zeroLightData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "halcyon::World found %zu lights in zero instance",
        m_zeroLightData->size());
    m_zeroGroup->setParamDirect("light", getParamDirect("light"));
  } else
    m_zeroGroup->removeParam("light");

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

  m_ispcInstances.resize(0);
  m_ispcInstances.reserve(m_instances.size());
  for (auto *i : m_instances)
    m_ispcInstances.push_back(i->getIE());

  ispc::World_setInstances(getIE(), m_ispcInstances.data());

  // cleanup() released the Embree scene, so the next scene update rebuilds
  // it even if no other object asked for a rebuild (like helide)
  m_objectUpdates = {};
}

const std::vector<Instance *> &World::instances() const
{
  return m_instances;
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

  ispc::World_setEmbreeScene(getIE(), embreeScene());

  // groups pick their volumes when their BLS is built, so this follows it;
  // the list ISPC reads is only replaced when it changes
  std::vector<void *> volumeInstances;
  for (auto *i : m_instances) {
    if (i->group()->hasVolumes())
      volumeInstances.push_back(i->getIE());
  }
  if (volumeInstances != m_ispcVolumeInstances) {
    m_ispcVolumeInstances = std::move(volumeInstances);
    ispc::World_setVolumeInstances(getIE(),
        m_ispcVolumeInstances.empty() ? nullptr : m_ispcVolumeInstances.data(),
        uint32_t(m_ispcVolumeInstances.size()));
  }

  updateLights();
  updateBounds();
}

std::optional<box3> World::bounds() const
{
  std::lock_guard<std::mutex> lock(m_boundsMutex);
  return m_bounds;
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
      "halcyon::World rebuilding %zu BLSs",
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
      "halcyon::World recommitting %zu BLSs",
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
      "halcyon::World rebuilding TLS over %zu instances",
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
            "halcyon::World rejecting empty surfaces in instance(%p) "
            "when building TLS",
            i);
      } else {
        reportMessage(ANARI_SEVERITY_DEBUG,
            "halcyon::World rejecting invalid surfaces in instance(%p) "
            "when building TLS",
            i);
      }
    }
    id++;
  });

  rtcCommitScene(m_embreeScene);
  m_objectUpdates.lastTLSBuild = helium::newTimeStamp();
}

void World::updateLights()
{
  // a light's records depend on the light, its group and its instance, so
  // any finalized object may change them; lights are few, so they are all
  // written again
  if (m_objectUpdates.lastLightsUpdate
      > deviceState()->commitBuffer.lastObjectFinalization())
    return;

  const size_t recordSize = ispc::sizeof_Light();
  m_lightRecords.clear();
  uint32_t numLights = 0;
  for (auto *i : m_instances) {
    if (!i->isValid())
      continue;
    for (auto *l : i->group()->lights()) {
      // UnknownObjects are never valid, so only real lights get past this
      if (!l || !l->isValid())
        continue;
      for (uint32_t x = 0; x < i->numTransforms(); x++) {
        m_lightRecords.resize((numLights + 1) * recordSize);
        if (l->writeWorldRecord(
                m_lightRecords.data() + numLights * recordSize, i->xfm(x)))
          numLights++;
      }
    }
  }
  m_lightRecords.resize(numLights * recordSize);

  reportMessage(ANARI_SEVERITY_DEBUG,
      "halcyon::World found %u lights in world space",
      numLights);
  ispc::World_setLights(
      getIE(), numLights ? m_lightRecords.data() : nullptr, numLights);
  m_objectUpdates.lastLightsUpdate = helium::newTimeStamp();
}

bool World::hasSurfaces() const
{
  return m_hasSurfaces;
}

void World::updateBounds()
{
  std::optional<box3> bounds;
  m_hasSurfaces = false;
  if (m_embreeScene) {
    bounds = getEmbreeSceneBounds(m_embreeScene);
    // the surfaces' bounds, empty (Embree's are +inf to -inf) without any
    m_hasSurfaces = bounds->lower.x <= bounds->upper.x
        && bounds->lower.y <= bounds->upper.y
        && bounds->lower.z <= bounds->upper.z;
    // helide adds the volumes' object-space bounds; these are transformed
    // like the instances' surfaces
    for (auto *i : instances()) {
      for (auto *v : i->group()->volumes()) {
        if (!v || !v->isRenderable())
          continue;
        for (uint32_t x = 0; x < i->numTransforms(); x++)
          bounds->extend(xfmBox(i->xfm(x), v->bounds()));
      }
    }
  }

  std::lock_guard<std::mutex> lock(m_boundsMutex);
  m_bounds = bounds;
}

void World::cleanup()
{
  rtcReleaseScene(m_embreeScene);
  m_embreeScene = nullptr;
  m_hasSurfaces = false;
  std::lock_guard<std::mutex> lock(m_boundsMutex);
  m_bounds.reset();
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::World *);
