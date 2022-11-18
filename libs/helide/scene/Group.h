// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/ObjectArray.h"
#include "light/Light.h"
#include "surface/Surface.h"
#include "volume/Volume.h"

namespace helide {

struct Group : public Object
{
  static size_t objectCount();

  Group(HelideGlobalState *s);
  ~Group() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint32_t flags) override;

  void commit() override;

  const std::vector<Surface *> &surfaces() const;

  void markCommitted() override;

  RTCScene embreeScene() const;
  void embreeSceneConstruct();
  void embreeSceneCommit();

 private:
  void cleanup();

  // Geometry //

  helium::IntrusivePtr<ObjectArray> m_surfaceData;
  std::vector<Surface *> m_surfaces;

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
