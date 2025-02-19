// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"

namespace hecore {

struct HeCoreDeviceGlobalState : public helium::BaseGlobalDeviceState
{
  // Add any data members here which all objects need to be able to access

  HeCoreDeviceGlobalState(ANARIDevice d);
};

// Helper functions/macros ////////////////////////////////////////////////////

inline HeCoreDeviceGlobalState *asHeCoreDeviceState(
    helium::BaseGlobalDeviceState *s)
{
  return (HeCoreDeviceGlobalState *)s;
}

#define HECORE_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HECORE_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace hecore
