// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
#include "Group_ispc.h"
// std
#include <iterator>

namespace halcyon {

Group::Group(HalcyonDeviceGlobalState *s)
    : Object(ANARI_GROUP, s, ispc::sizeof_Group()),
      m_surfaceData(this),
      m_volumeData(this),
      m_lightData(this)
{
  ispc::Group_setVolumes(getIE(), nullptr, 0);
}

Group::~Group()
{
  cleanup();
}

bool Group::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    if (flags & ANARI_WAIT) {
      embreeSceneConstruct();
      embreeSceneCommit();
    }
    const box3 b = bounds();
    std::memcpy(ptr, &b, sizeof(b));
    return true;
  }

  return Object::getProperty(name, type, ptr, size, flags);
}

void Group::commitParameters()
{
  m_surfaceData = getParamObject<ObjectArray>("surface");
  m_volumeData = getParamObject<ObjectArray>("volume");
  m_lightData = getParamObject<ObjectArray>("light");
}

void Group::finalize()
{
  cleanup();

  // surfaces are picked in embreeSceneConstruct(): their validity depends on
  // device parameters that can change after the group is finalized
  m_volumes.clear();
  m_lights.clear();

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

void Group::markFinalized()
{
  Object::markFinalized();
  deviceState()->objectUpdates.lastBLSReconstructSceneRequest =
      helium::newTimeStamp();
}

const std::vector<Surface *> &Group::surfaces() const
{
  return m_surfaces;
}

const std::vector<Volume *> &Group::volumes() const
{
  return m_volumes;
}

box3 Group::bounds() const
{
  box3 b = m_embreeScene ? getEmbreeSceneBounds(m_embreeScene) : box3();
  for (auto *v : m_volumes) {
    if (v && v->isRenderable())
      b.extend(v->bounds());
  }
  return b;
}

bool Group::hasVolumes() const
{
  return !m_ispcVolumes.empty();
}

const std::vector<Light *> &Group::lights() const
{
  return m_lights;
}

RTCScene Group::embreeScene() const
{
  return m_embreeScene;
}

void Group::embreeSceneConstruct()
{
  const auto &state = *deviceState();
  if (m_objectUpdates.lastSceneConstruction
      > state.objectUpdates.lastBLSReconstructSceneRequest)
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "halcyon::Group rebuilding embree scene");

  rtcReleaseScene(m_embreeScene);
  m_embreeScene = rtcNewScene(deviceState()->embreeDevice);

  m_surfaces.clear();
  m_ispcSurfaces.clear();

  if (m_surfaceData) {
    std::for_each(m_surfaceData->handlesBegin(),
        m_surfaceData->handlesEnd(),
        [&](auto *o) {
          auto *s = (Surface *)o;
          if (s && s->isValid()) {
            if (s->isVisible()) {
              m_surfaces.push_back(s);
              m_ispcSurfaces.push_back(s->getIE());
            }
            return;
          }

          reportMessage(ANARI_SEVERITY_DEBUG,
              "halcyon::Group rejecting invalid surface(%p) in building BLS",
              s);
          if (!s)
            return;
          auto *g = s->geometry();
          if (!g || !g->isValid()) {
            reportMessage(
                ANARI_SEVERITY_DEBUG, "    halcyon::Geometry is invalid");
          }
          auto *m = s->material();
          if (!m || !m->isValid()) {
            reportMessage(
                ANARI_SEVERITY_DEBUG, "    halcyon::Material is invalid");
          }
        });
  }

  // Embree geometry IDs index the ISPC surface list
  uint32_t id = 0;
  for (auto *s : m_surfaces)
    rtcAttachGeometryByID(m_embreeScene, s->geometry()->embreeGeometry(), id++);

  ispc::Group_setSurfaces(getIE(),
      m_ispcSurfaces.empty() ? nullptr : m_ispcSurfaces.data(),
      m_ispcSurfaces.size());

  // like surfaces, a volume's validity (its field's) can change after the
  // group is finalized
  m_ispcVolumes.clear();
  for (auto *v : m_volumes) {
    if (v && v->isRenderable())
      m_ispcVolumes.push_back(v->getIE());
  }
  ispc::Group_setVolumes(getIE(),
      m_ispcVolumes.empty() ? nullptr : m_ispcVolumes.data(),
      m_ispcVolumes.size());

  m_objectUpdates.lastSceneConstruction = helium::newTimeStamp();
  m_objectUpdates.lastSceneCommit = 0;
  embreeSceneCommit();
}

void Group::embreeSceneCommit()
{
  const auto &state = *deviceState();
  if (!m_embreeScene
      || m_objectUpdates.lastSceneCommit
          > state.objectUpdates.lastBLSCommitSceneRequest)
    return;

  reportMessage(ANARI_SEVERITY_DEBUG, "halcyon::Group committing embree scene");

  rtcCommitScene(m_embreeScene);
  m_objectUpdates.lastSceneCommit = helium::newTimeStamp();
}

void Group::cleanup()
{
  m_surfaces.clear();
  m_volumes.clear();
  m_ispcVolumes.clear();
  ispc::Group_setVolumes(getIE(), nullptr, 0);

  m_objectUpdates.lastSceneConstruction = 0;
  m_objectUpdates.lastSceneCommit = 0;

  rtcReleaseScene(m_embreeScene);
  m_embreeScene = nullptr;
}

box3 getEmbreeSceneBounds(RTCScene scene)
{
  RTCBounds eb;
  rtcGetSceneBounds(scene, &eb);
  return box3({eb.lower_x, eb.lower_y, eb.lower_z},
      {eb.upper_x, eb.upper_y, eb.upper_z});
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Group *);
