// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Sampler.h"
// subtypes
#include "Image1D.h"
#include "Image2D.h"
#include "TransformSampler.h"

namespace helide {

Sampler::Sampler(HelideGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler *Sampler::createInstance(std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  else if (subtype == "image2D")
    return new Image2D(s);
  else if (subtype == "transform")
    return new TransformSampler(s);
  else
    return (Sampler *)new UnknownObject(ANARI_SAMPLER, s);
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::Sampler *);
