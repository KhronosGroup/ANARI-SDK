// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"

namespace anari {
namespace example_device {

FactoryMapPtr<Volume> Volume::g_volumes;

ANARIParameter Volume::g_parameters[] = {
  {"field", ANARI_SPATIAL_FIELD},
  {"valueRange", ANARI_FLOAT32_VEC2},
  {"color", ANARI_ARRAY1D},
  {"opacity", ANARI_ARRAY1D},
  {NULL, ANARI_UNKNOWN},
};

void Volume::init()
{
  g_volumes = std::make_unique<FactoryMap<Volume>>();

  g_volumes->emplace("density", []() -> Volume * { return new Volume; });
  g_volumes->emplace("scivis", []() -> Volume * { return new Volume; });
}

Volume *Volume::createInstance(const char *type)
{
  if (g_volumes.get() == nullptr)
    init();

  auto *fcn = (*g_volumes)[type];

  if (fcn)
    return fcn();
  else {
    throw std::runtime_error("could not create volume");
  }
}

ANARIParameter *Volume::parameters(const char *_type)
{
  std::string type(_type);
  if (type == "density" || type == "scivis") {
    return Volume::g_parameters;
  } else {
    return nullptr;
  }
}

Volume::Volume()
{
  setCommitPriority(COMMIT_PRIORITY_VOLUME);
}

void Volume::commit()
{
  m_volume = getParamObject<SpatialField>("field");
  SceneObject::setBounds(m_volume ? m_volume->bounds() : box3());

  auto range = getParam<vec2>("valueRange", vec2(0.f, 1.f));
  m_minmax.lower = range.x;
  m_minmax.upper = range.y;
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
