// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
#include "SpatialField_ispc.h"
#include "array/Array3D.h"
// embree
#include "algorithms/parallel_for.h"
// std
#include <algorithm>
#include <cstring>
#include <vector>

namespace halcyon {

// SpatialField subtypes //////////////////////////////////////////////////////

// structuredRegular: 'data' voxels (FLOAT32, FLOAT64, UFIXED8, UFIXED16 or
// FIXED16) placed at 'origin' + index * 'spacing' (positive; helide doesn't
// check it). The voxels are copied at
// finalize in their own type, so 8/16-bit volumes don't grow 2-4x as floats.
// Each finalize also computes the value range of each macrocell (8^3 voxel
// intervals) for the pathtracer's majorants.
struct StructuredRegularField : public SpatialField
{
  StructuredRegularField(HalcyonDeviceGlobalState *s) : SpatialField(s) {}

  bool isValid() const override
  {
    return SpatialField::isValid() && m_valid;
  }

  void commitParameters() override
  {
    SpatialField::commitParameters();
    m_data = getParamObject<Array3D>("data");
    m_origin = getParam<float3>("origin", float3(0.f));
    m_spacing = getParam<float3>("spacing", float3(1.f));
  }

  void finalize() override
  {
    m_voxels.clear();
    m_macrocellRanges.clear();
    m_valid = false;

    if (!m_data) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'data' on 'structuredRegular' field");
      ispc::SpatialField_setDefaults(getIE());
      return;
    }

    const anari::DataType type = m_data->elementType();
    const size_t bytes = m_data->totalSize() * anari::sizeOf(type);
    // std::vector's allocation is aligned for any voxel type
    m_voxels.resize(bytes);
    if (bytes > 0)
      std::memcpy(m_voxels.data(), m_data->data(), bytes);

    const uint3 dims = m_data->size();
    m_valid = ispc::SpatialField_setStructuredRegular(getIE(),
        m_voxels.empty() ? nullptr : m_voxels.data(),
        type,
        &dims.x,
        &m_origin.x,
        &m_spacing.x);

    if (!m_valid) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "invalid 'structuredRegular' field: 'data' is empty or has "
          "element type '%s' (expected FLOAT32, FLOAT64, UFIXED8, UFIXED16 "
          "or FIXED16), or 'spacing' isn't positive",
          anari::toString(type));
      m_voxels.clear();
      return;
    }

    // in chunks of cells on Embree's threads
    const size_t cells = ispc::SpatialField_numMacrocells(getIE());
    m_macrocellRanges.resize(2 * cells);
    constexpr size_t CHUNK = 64;
    embree::parallel_for((cells + CHUNK - 1) / CHUNK, [&](size_t chunk) {
      ispc::SpatialField_computeMacrocellRanges(getIE(),
          m_macrocellRanges.data(),
          chunk * CHUNK,
          std::min(cells, (chunk + 1) * CHUNK));
    });
    ispc::SpatialField_setMacrocellRanges(getIE(), m_macrocellRanges.data());
  }

 private:
  helium::ChangeObserverPtr<Array3D> m_data{this};
  float3 m_origin{0.f};
  float3 m_spacing{1.f};
  std::vector<uint8_t> m_voxels;
  // each macrocell's smallest and largest value (packed float2), as ISPC's
  // SpatialField::macrocellRanges reads them
  std::vector<float> m_macrocellRanges;
  bool m_valid{false};
};

// Base SpatialField definitions //////////////////////////////////////////////

SpatialField::SpatialField(HalcyonDeviceGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s, ispc::sizeof_SpatialField())
{
  // helium only finalizes objects with committed parameters: until then the
  // field has no hook
  ispc::SpatialField_setDefaults(getIE());
}

SpatialField *SpatialField::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegularField(s);
  else {
    // volumes may reference unknown fields too, which are never sampled
    auto *unknown =
        new UnknownObject(ANARI_SPATIAL_FIELD, s, ispc::sizeof_SpatialField());
    ispc::SpatialField_setDefaults(unknown->getIE());
    return (SpatialField *)unknown;
  }
}

box3 SpatialField::bounds() const
{
  box3 b;
  ispc::SpatialField_getBounds(getIE(), &b.lower.x, &b.upper.x);
  return b;
}

float SpatialField::stepSize() const
{
  return ispc::SpatialField_getStepSize(getIE());
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::SpatialField *);
