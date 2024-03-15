// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "RenderingSemaphore.h"
#include "HelideMath.h"
// helium
#include "helium/BaseGlobalDeviceState.h"
// embree
#include "embree3/rtcore.h"

namespace helide {

struct Frame;

struct HelideGlobalState : public helium::BaseGlobalDeviceState
{
  int numThreads{1};

  struct ObjectUpdates
  {
    helium::TimeStamp lastBLSReconstructSceneRequest{0};
    helium::TimeStamp lastBLSCommitSceneRequest{0};
    helium::TimeStamp lastTLSReconstructSceneRequest{0};
  } objectUpdates;

  RenderingSemaphore renderingSemaphore;
  Frame *currentFrame{nullptr};

  RTCDevice embreeDevice{nullptr};

  bool allowInvalidSurfaceMaterials{true};
  float4 invalidMaterialColor{1.f, 0.f, 1.f, 1.f};

  // Helper methods //

  HelideGlobalState(ANARIDevice d);
  void waitOnCurrentFrame() const;
};

// Helper functions/macros ////////////////////////////////////////////////////

inline HelideGlobalState *asHelideState(helium::BaseGlobalDeviceState *s)
{
  return (HelideGlobalState *)s;
}

#define HELIDE_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HELIDE_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace helide
