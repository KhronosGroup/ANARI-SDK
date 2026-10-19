// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Instance.h"
// std
#include <mutex>
#include <optional>

namespace halcyon {

struct World : public Object
{
  World(HalcyonDeviceGlobalState *s);
  ~World() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  const std::vector<Instance *> &instances() const;

  RTCScene embreeScene() const;
  void embreeSceneUpdate();
  // The "bounds" property as of the last scene update: none before the first
  // update, empty (lower above upper) for a world with nothing in it
  std::optional<box3> bounds() const;
  // Whether the last scene update left surfaces that rays can hit: false
  // before the first update and for a world with nothing or only volumes
  bool hasSurfaces() const;

 private:
  void rebuildBLSs();
  void recommitBLSs();
  void rebuildTLS();
  void updateLights();
  void updateBounds();
  void cleanup();

  helium::ChangeObserverPtr<ObjectArray> m_zeroSurfaceData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroVolumeData;
  helium::ChangeObserverPtr<ObjectArray> m_zeroLightData;

  helium::ChangeObserverPtr<ObjectArray> m_instanceData;
  std::vector<Instance *> m_instances;
  std::vector<void *> m_ispcInstances;
  // the instances whose group has volumes, shared w/ ISPC
  std::vector<void *> m_ispcVolumeInstances;
  // every valid light of every instance, once per instance transform, as
  // world-space records (sizeof_Light() bytes each), shared w/ ISPC
  std::vector<uint8_t> m_lightRecords;

  helium::IntrusivePtr<Group> m_zeroGroup;
  helium::IntrusivePtr<Instance> m_zeroInstance;

  struct ObjectUpdates
  {
    helium::TimeStamp lastTLSBuild{0};
    helium::TimeStamp lastBLSReconstructCheck{0};
    helium::TimeStamp lastBLSCommitCheck{0};
    helium::TimeStamp lastLightsUpdate{0};
  } m_objectUpdates;

  RTCScene m_embreeScene{nullptr};

  // The "bounds" property as of the last scene update, which runs on the
  // task queue; read without waiting from any thread
  mutable std::mutex m_boundsMutex;
  std::optional<box3> m_bounds;
  // updated with the bounds; read on the task queue only
  bool m_hasSurfaces{false};
};

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::World *, ANARI_WORLD);
