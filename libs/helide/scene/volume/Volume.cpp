// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
// subtypes
#include "TransferFunction1D.h"

namespace helide {

Volume::Volume(HelideGlobalState *s) : Object(ANARI_VOLUME, s)
{
  s->objectCounts.volumes++;
}

Volume::~Volume()
{
  deviceState()->objectCounts.volumes--;
}

Volume *Volume::createInstance(std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  else
    return (Volume *)new UnknownObject(ANARI_VOLUME, s);
}

void Volume::commit()
{
  m_id = getParam<uint32_t>("id", ~0u);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Volume *);
