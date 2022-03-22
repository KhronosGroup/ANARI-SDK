// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
// specific types
#include "StructuredRegularField.h"

namespace anari {
namespace example_device {

FactoryMapPtr<SpatialField> SpatialField::g_spatialFields;

void SpatialField::init()
{
  g_spatialFields = std::make_unique<FactoryMap<SpatialField>>();

  g_spatialFields->emplace("structuredRegular",
      []() -> SpatialField * { return new StructuredRegularField; });
}

SpatialField::SpatialField()
{
  setCommitPriority(COMMIT_PRIORITY_SPATIAL_FIELD);
}

SpatialField *SpatialField::createInstance(const char *type)
{
  if (g_spatialFields.get() == nullptr)
    init();

  auto *fcn = (*g_spatialFields)[type];

  if (fcn)
    return fcn();
  else {
    throw std::runtime_error("could not create geometry");
  }
}

void SpatialField::setStepSize(float size)
{
  m_stepSize = size;
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::SpatialField *);

} // namespace anari
