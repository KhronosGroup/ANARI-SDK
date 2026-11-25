// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace halcyon {

// Inherit from this, add your functionality, and add it to 'createInstance()'.
// Every volume (including unknown subtypes, which are never valid) has an
// ISPC object; groups only hand valid, visible volumes to ISPC.
struct Volume : public Object
{
  Volume(HalcyonDeviceGlobalState *d);
  virtual ~Volume() = default;
  static Volume *createInstance(
      std::string_view subtype, HalcyonDeviceGlobalState *d);

  void commitParameters() override;
  void markFinalized() override;

  uint32_t id() const;
  bool isVisible() const;
  // Valid and visible: groups render and bound only these
  bool isRenderable() const;

  // Object-space bounds; only meaningful for valid volumes
  virtual box3 bounds() const;

 private:
  uint32_t m_id{~0u};
  bool m_visible{true};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Volume::id() const
{
  return m_id;
}

inline bool Volume::isVisible() const
{
  return m_visible;
}

inline bool Volume::isRenderable() const
{
  return isValid() && isVisible();
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_SPECIALIZATION(halcyon::Volume *, ANARI_VOLUME);
