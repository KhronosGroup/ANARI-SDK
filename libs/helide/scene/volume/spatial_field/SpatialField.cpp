// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
// subtypes
#include "StructuredRegularField.h"

namespace helide {

SpatialField::SpatialField(HelideGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{}

SpatialField *SpatialField::createInstance(
    std::string_view subtype, HelideGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegularField(s);
  else
    return (SpatialField *)new UnknownObject(ANARI_SPATIAL_FIELD, s);
}

void SpatialField::setStepSize(float size)
{
  m_stepSize = size;
}

} // namespace helide

HELIDE_ANARI_TYPEFOR_DEFINITION(helide::SpatialField *);
