// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// embree
#include "embree3/rtcore.h"

namespace helide {

struct Frame;

struct HelideGlobalState : public helium::BaseGlobalDeviceState
{
  int numThreads{1};

  struct ObjectCounts
  {
    size_t frames{0};
    size_t cameras{0};
    size_t renderers{0};
    size_t worlds{0};
    size_t instances{0};
    size_t groups{0};
    size_t surfaces{0};
    size_t geometries{0};
    size_t materials{0};
    size_t arrays{0};
    size_t unknown{0};
  } objectCounts;

  struct ObjectUpdates
  {
    helium::TimeStamp lastBLSReconstructSceneRequest{0};
    helium::TimeStamp lastBLSCommitSceneRequest{0};
    helium::TimeStamp lastTLSReconstructSceneRequest{0};
  } objectUpdates;

  Frame *currentFrame{nullptr};

  RTCDevice embreeDevice{nullptr};

  // Helper methods //

  HelideGlobalState(ANARIDevice d);
  void waitOnCurrentFrame() const;
};

} // namespace helide
