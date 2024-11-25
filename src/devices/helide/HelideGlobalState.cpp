// Copyright 2023-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HelideGlobalState.h"
#include "frame/Frame.h"

namespace helide {

HelideGlobalState::HelideGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{}

void HelideGlobalState::waitOnCurrentFrame() const
{
  if (currentFrame)
    currentFrame->wait();
}

} // namespace helide