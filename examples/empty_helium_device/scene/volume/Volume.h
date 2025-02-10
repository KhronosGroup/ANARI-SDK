// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"

namespace hecore {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Volume : public Object
{
  Volume(HeCoreDeviceGlobalState *d);
  virtual ~Volume() = default;
  static Volume *createInstance(
      std::string_view subtype, HeCoreDeviceGlobalState *d);

  void commitParameters() override;

  uint32_t id() const;

 private:
  uint32_t m_id{~0u};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline uint32_t Volume::id() const
{
  return m_id;
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_SPECIALIZATION(hecore::Volume *, ANARI_VOLUME);
