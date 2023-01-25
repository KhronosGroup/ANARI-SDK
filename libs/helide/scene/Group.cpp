// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"

namespace helide {

Group::Group(HelideGlobalState *s) : Object(ANARI_GROUP, s)
{
  s->objectCounts.groups++;
}

Group::~Group()
{
  cleanup();
  deviceState()->objectCounts.groups--;
}

bool Group::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    if (flags & ANARI_WAIT) {
      deviceState()->commitBuffer.flush();
      embreeSceneConstruct();
      embreeSceneCommit();
    }
    auto bounds = getEmbreeSceneBounds(m_embreeScene);
    std::memcpy(ptr, &bounds, sizeof(bounds));
    return true;
  }

  return Object::getProperty(name, type, ptr, flags);
}

void Group::commit()
{
  cleanup();

  m_surfaceData = getParamObject<ObjectArray>("surface");

  if (!m_surfaceData)
    return;

  m_surfaceData->addCommitObserver(this);
}

const std::vector<Surface *> &Group::surfaces() const
{
  return m_surfaces;
}

void Group::markCommitted()
{
  Object::markCommitted();
  deviceState()->objectUpdates.lastBLSReconstructSceneRequest =
      helium::newTimeStamp();
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

  reportMessage(
      ANARI_SEVERITY_DEBUG, "helide::Group(%p) rebuilding embree scene", this);

  rtcReleaseScene(m_embreeScene);
  m_embreeScene = rtcNewScene(deviceState()->embreeDevice);

  if (m_surfaceData) {
    uint32_t id = 0;
    std::for_each(m_surfaceData->handlesBegin(),
        m_surfaceData->handlesEnd(),
        [&](Object *o) {
          auto *s = (Surface *)o;
          if (s && s->isValid()) {
            m_surfaces.push_back(s);
            rtcAttachGeometryByID(
                m_embreeScene, s->geometry()->embreeGeometry(), id);
          } else {
            reportMessage(ANARI_SEVERITY_DEBUG,
                "helide::Group rejecting invalid surface(%p) in building BLS",
                s);
          }
          id++;
        });
  }

  m_objectUpdates.lastSceneConstruction = helium::newTimeStamp();
  m_objectUpdates.lastSceneCommit = 0;
  embreeSceneCommit();
}

void Group::embreeSceneCommit()
{
  const auto &state = *deviceState();
  if (m_objectUpdates.lastSceneCommit
      > state.objectUpdates.lastBLSCommitSceneRequest)
    return;

  reportMessage(
      ANARI_SEVERITY_DEBUG, "helide::Group(%p) committing embree scene", this);

  rtcCommitScene(m_embreeScene);
  m_objectUpdates.lastSceneCommit = helium::newTimeStamp();
}

void Group::cleanup()
{
  if (m_surfaceData)
    m_surfaceData->removeCommitObserver(this);

  m_surfaces.clear();

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

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Group *);
