// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/ObjectArray.h"
#include "light/Light.h"
#include "surface/Surface.h"
#include "volume/Volume.h"

namespace halcyon {

struct Group : public Object
{
  Group(HalcyonDeviceGlobalState *s);
  ~Group() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;
  void markFinalized() override;

  const std::vector<Surface *> &surfaces() const;
  const std::vector<Volume *> &volumes() const;
  const std::vector<Light *> &lights() const;

  // Bounds of the surfaces (as of the last BLS build) and of the valid,
  // visible volumes, in object space
  box3 bounds() const;
  // Whether the last BLS build found valid, visible volumes
  bool hasVolumes() const;

  RTCScene embreeScene() const;
  void embreeSceneConstruct();
  void embreeSceneCommit();

 private:
  void cleanup();

  helium::ChangeObserverPtr<ObjectArray> m_surfaceData;
  std::vector<Surface *> m_surfaces;
  std::vector<void *> m_ispcSurfaces;

  helium::ChangeObserverPtr<ObjectArray> m_volumeData;
  std::vector<Volume *> m_volumes;
  // the valid, visible volumes as of the last BLS build
  std::vector<void *> m_ispcVolumes;

  helium::ChangeObserverPtr<ObjectArray> m_lightData;
  std::vector<Light *> m_lights;

  struct ObjectUpdates
  {
    helium::TimeStamp lastSceneConstruction{0};
    helium::TimeStamp lastSceneCommit{0};
  } m_objectUpdates;

  RTCScene m_embreeScene{nullptr};
};

box3 getEmbreeSceneBounds(RTCScene scene);

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Group *, ANARI_GROUP);
