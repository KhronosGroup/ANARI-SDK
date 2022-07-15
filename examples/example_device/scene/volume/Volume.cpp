// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"

namespace anari {
namespace example_device {

Volume::Volume()
{
  setCommitPriority(COMMIT_PRIORITY_VOLUME);
}

void Volume::commit()
{
  m_volume = getParamObject<SpatialField>("field");
  SceneObject::setBounds(m_volume ? m_volume->bounds() : box3());

  m_minmax = getParam<box1>("valueRange", box1(0.f, 1.f));
  m_invSize = 1.f / size(m_minmax);

  m_colorData = getParamObject<Array1D>("color");
  m_opacityData = getParamObject<Array1D>("opacity");

  if (!m_colorData)
    throw std::runtime_error("no color data provided to transfer function");

  if (!m_opacityData)
    throw std::runtime_error("no opacity data provided to transfer function");

  m_colorSampler =
      make_sampler(m_colorData->dataAs<vec3>(), m_colorData->size());
  m_opacitySampler =
      make_sampler(m_opacityData->dataAs<float>(), m_opacityData->size());
}

} // namespace example_device

ANARI_TYPEFOR_DEFINITION(example_device::Volume *);

} // namespace anari
