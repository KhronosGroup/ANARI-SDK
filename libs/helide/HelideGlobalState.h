// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// embree
#include "embree3/rtcore.h"

namespace helide {

struct HelideGlobalState : public helium::BaseGlobalDeviceState
{
  int numThreads{1};

  struct ObjectUpdates
  {
    helium::TimeStamp lastBLSReconstructSceneRequest{0};
    helium::TimeStamp lastBLSCommitSceneRequest{0};
    helium::TimeStamp lastTLSReconstructSceneRequest{0};
  } objectUpdates;

  RTCDevice embreeDevice{nullptr};

  // Helper methods //

  HelideGlobalState(ANARIDevice d) : helium::BaseGlobalDeviceState(d) {}
};

} // namespace helide
