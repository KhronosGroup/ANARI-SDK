// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/ObjectArray.h"
#include "light/Light.h"
#include "surface/Surface.h"
#include "volume/Volume.h"

namespace helide {

struct Group : public Object
{
  Group(HelideGlobalState *s);
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

  void intersectVolumes(VolumeRay &ray, const mat4 &invMat) const;

  RTCScene embreeScene() const;
  void embreeSceneConstruct();
  void embreeSceneCommit();

 private:
  void cleanup();

  // Geometry //

  helium::ChangeObserverPtr<ObjectArray> m_surfaceData;
  std::vector<Surface *> m_surfaces;

  // Volume //

  helium::ChangeObserverPtr<ObjectArray> m_volumeData;
  std::vector<Volume *> m_volumes;

  // BVH //

  struct ObjectUpdates
  {
    helium::TimeStamp lastSceneConstruction{0};
    helium::TimeStamp lastSceneCommit{0};
  } m_objectUpdates;

  RTCScene m_embreeScene{nullptr};
};

box3 getEmbreeSceneBounds(RTCScene scene);

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Group *, ANARI_GROUP);
