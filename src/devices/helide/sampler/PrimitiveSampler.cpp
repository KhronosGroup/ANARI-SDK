// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PrimitiveSampler.h"
#include "geometry/Geometry.h"

namespace helide {

PrimitiveSampler::PrimitiveSampler(HelideGlobalState *s) : Sampler(s) {}

bool PrimitiveSampler::isValid() const
{
  return Sampler::isValid() && m_array;
}

void PrimitiveSampler::commitParameters()
{
  Sampler::commitParameters();
  m_array = getParamObject<Array1D>("array");
  // 'inOffset' is the spec name; 'offset' is helide's older name, still read
  // so existing scenes keep rendering. A set 'inOffset' always wins, even if
  // its type is unsupported.
  m_offset = uint32_t(
      hasParam("inOffset") ? readOffset("inOffset") : readOffset("offset"));
}

uint64_t PrimitiveSampler::readOffset(const std::string &name) const
{
  if (!hasParam(name))
    return 0;
  if (hasParam(name, ANARI_UINT64))
    return getParam<uint64_t>(name, 0);
  if (hasParam(name, ANARI_UINT32))
    return getParam<uint32_t>(name, 0);
  reportMessage(ANARI_SEVERITY_WARNING,
      "ignoring primitive sampler parameter '%s' of type %s (expected %s)",
      name.c_str(),
      anari::toString(getParamDirect(name).type()),
      anari::toString(ANARI_UINT64));
  return 0;
}

float4 PrimitiveSampler::getSample(const Geometry &g,
    const Ray &r,
    const UniformAttributeSet & /*instAttrV*/) const
{
  return attributeValueAt(*m_array, int32_t(r.primID + m_offset));
}

} // namespace helide
