// Copyright 2024-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"

namespace hecore {

Sampler::Sampler(HeCoreDeviceGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler *Sampler::createInstance(
    std::string_view subtype, HeCoreDeviceGlobalState *s)
{
  return (Sampler *)new UnknownObject(ANARI_SAMPLER, s);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Sampler *);
