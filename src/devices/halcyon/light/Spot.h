// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Light.h"

namespace halcyon {

// The 'spot' light (KHR_LIGHT_SPOT): a delta point emitting into a cone
// around 'direction' (Light.h has the units and the cone)
struct Spot : public Light
{
  Spot(HalcyonDeviceGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool writeWorldRecord(void *record, const mat4 &xfm) const override;

 private:
  float3 m_position{0.f};
  // 'direction', normalized; zero if it can't be
  float3 m_direction{0.f, 0.f, -1.f};
  // the intensity along the axis, times 'color'
  float3 m_emission{1.f};
};

} // namespace halcyon
