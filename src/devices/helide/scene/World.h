// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"

namespace helide {

struct World : public Object
{
  World(HelideGlobalState *s);
  ~World() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  const std::vector<Instance *> &instances() const;

  void intersectVolumes(VolumeRay &ray) const;

  const Instance *instanceFromRay(const Ray &ray) const;
  const Instance *instanceFromRay(const VolumeRay &ray) const;
  const Surface *surfaceFromRay(const Ray &ray) const;

  RTCScene embreeScene() const;
  void embreeSceneUpdate();

 private:
  void rebuildBLSs();
  void recommitBLSs();
  void rebuildTLS();
  void cleanup();

  helium::ChangeObserverPtr<ObjectArray> m_zeroSurfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroVolumeData;

  helium::ChangeObserverPtr<ObjectArray> m_instanceData;
  std::vector<Instance *> m_instances;

  bool m_addZeroInstance{false};
  helium::IntrusivePtr<Group> m_zeroGroup;
  helium::IntrusivePtr<Instance> m_zeroInstance;

  size_t m_numSurfaceInstances{0};

  box3 m_surfaceBounds;

  struct ObjectUpdates
  {
    helium::TimeStamp lastTLSBuild{0};
    helium::TimeStamp lastBLSReconstructCheck{0};
    helium::TimeStamp lastBLSCommitCheck{0};
  } m_objectUpdates;

  RTCScene m_embreeScene{nullptr};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const Instance *World::instanceFromRay(const Ray &ray) const
{
  return instances()[ray.instID];
}

inline const Instance *World::instanceFromRay(const VolumeRay &ray) const
{
  return instances()[ray.instID];
}

inline const Surface *World::surfaceFromRay(const Ray &ray) const
{
  auto *inst = instanceFromRay(ray);
  return inst->group()->surfaces()[ray.geomID];
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::World *, ANARI_WORLD);
