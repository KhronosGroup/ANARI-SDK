// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Group.h"
// std
#include <iterator>

namespace helide {

Group::Group(HelideGlobalState *s)
    : Object(ANARI_GROUP, s), m_surfaceData(this), m_volumeData(this)
{}

Group::~Group()
{
  cleanup();
}

bool Group::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    if (flags & ANARI_WAIT) {
      embreeSceneConstruct();
      embreeSceneCommit();
    }
    auto bounds = getEmbreeSceneBounds(m_embreeScene);
    for (auto *v : volumes()) {
      if (v->isValid())
        bounds.extend(v->bounds());
    }
    std::memcpy(ptr, &bounds, sizeof(bounds));
    return true;
  }

  return Object::getProperty(name, type, ptr, flags);
}

void Group::commit()
{
  cleanup();

  m_surfaceData = getParamObject<ObjectArray>("surface");
  m_volumeData = getParamObject<ObjectArray>("volume");

  if (m_volumeData) {
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

void Group::intersectVolumes(VolumeRay &ray) const
{
  Volume *originalVolume = ray.volume;
  box1 t = ray.t;

  for (auto *v : volumes()) {
    if (!v->isValid())
      continue;
    const box3 bounds = v->bounds();
    const float3 mins = (bounds.lower - ray.org) * (1.f / ray.dir);
    const float3 maxs = (bounds.upper - ray.org) * (1.f / ray.dir);
    const float3 nears = linalg::min(mins, maxs);
    const float3 fars = linalg::max(mins, maxs);

    const box1 lt(linalg::maxelem(nears), linalg::minelem(fars));

    if (lt.lower < lt.upper && (!ray.volume || lt.lower < t.lower)) {
      t.lower = clamp(lt.lower, t);
      t.upper = clamp(lt.upper, t);
      ray.volume = v;
    }
  }

  if (ray.volume != originalVolume)
    ray.t = t;
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

  reportMessage(ANARI_SEVERITY_DEBUG, "helide::Group rebuilding embree scene");

  rtcReleaseScene(m_embreeScene);
  m_embreeScene = rtcNewScene(deviceState()->embreeDevice);

  if (m_surfaceData) {
    uint32_t id = 0;
    std::for_each(m_surfaceData->handlesBegin(),
        m_surfaceData->handlesEnd(),
        [&](auto *o) {
          auto *s = (Surface *)o;
          if (s && s->isValid()) {
            m_surfaces.push_back(s);
            rtcAttachGeometryByID(
                m_embreeScene, s->geometry()->embreeGeometry(), id++);
          } else {
            reportMessage(ANARI_SEVERITY_DEBUG,
                "helide::Group rejecting invalid surface(%p) in building BLS",
                s);
            auto *g = s->geometry();
            if (!g || !g->isValid()) {
              reportMessage(
                  ANARI_SEVERITY_DEBUG, "    helide::Geometry is invalid");
            }
            auto *m = s->material();
            if (!m || !m->isValid()) {
              reportMessage(
                  ANARI_SEVERITY_DEBUG, "    helide::Material is invalid");
            }
          }
        });
  }

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

  reportMessage(ANARI_SEVERITY_DEBUG, "helide::Group committing embree scene");

  rtcCommitScene(m_embreeScene);
  m_objectUpdates.lastSceneCommit = helium::newTimeStamp();
}

void Group::cleanup()
{
  m_surfaces.clear();
  m_volumes.clear();

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
