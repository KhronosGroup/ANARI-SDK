// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
// subtypes
#include "SciVisVolume.h"

namespace helide {

Volume::Volume(HelideGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume *Volume::createInstance(std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "scivis")
    return new SciVisVolume(s);
  else
    return (Volume *)new UnknownObject(ANARI_VOLUME, s);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Volume *);
