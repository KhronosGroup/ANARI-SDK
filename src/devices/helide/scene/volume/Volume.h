// Copyright 2021-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace helide {

struct Volume : public Object
{
  Volume(HelideGlobalState *d);
  virtual ~Volume() = default;
  static Volume *createInstance(std::string_view subtype, HelideGlobalState *d);

  void commitParameters() override;

  uint32_t id() const;

  virtual box3 bounds() const = 0;
  virtual void render(
      const VolumeRay &vray,
      float invVolumeSamplingRate,
      float3 &outputColor,
      float &outputOpacity) = 0;

  private:
  uint32_t m_id{~0u};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Volume::id() const
{
  return m_id;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_SPECIALIZATION(helide::Volume *, ANARI_VOLUME);
