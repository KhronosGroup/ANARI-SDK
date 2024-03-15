// Copyright 2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"

namespace hecore {

Volume::Volume(HeCoreDeviceGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume *Volume::createInstance(
    std::string_view subtype, HeCoreDeviceGlobalState *s)
{
  return (Volume *)new UnknownObject(ANARI_VOLUME, s);
}

void Volume::commit()
{
  m_id = getParam<uint32_t>("id", ~0u);
}

} // namespace hecore

HECORE_ANARI_TYPEFOR_DEFINITION(hecore::Volume *);
