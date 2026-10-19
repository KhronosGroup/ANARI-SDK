// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
#include "Volume_ispc.h"
#include "array/Array1D.h"
#include "spatial_field/SpatialField.h"
#include "SpatialField_ispc.h"
// embree
#include "algorithms/parallel_for.h"
// std
#include <algorithm>
#include <cmath>
#include <vector>

namespace halcyon {

// Volume subtypes ////////////////////////////////////////////////////////////

// transferFunction1D: the 'value' field's samples, mapped through 'valueRange'
// to [0, 1], look up 'color' and 'opacity' (arrays linearly interpolated, or
// uniform values). A sample's opacity is 'opacity' times its color's alpha.
struct TransferFunction1D : public Volume
{
  TransferFunction1D(HalcyonDeviceGlobalState *s) : Volume(s) {}

  bool isValid() const override
  {
    return m_field && m_field->isValid() && unitDistanceIsValid();
  }

  box3 bounds() const override
  {
    return m_field ? m_field->bounds() : box3();
  }

  void commitParameters() override
  {
    Volume::commitParameters();
    m_field = getParamObject<SpatialField>("value");

    m_valueRange = getParam<box1>("valueRange", box1(0.f, 1.f));
    double valueRange_d[2] = {0.0, 1.0};
    if (getParam("valueRange", ANARI_FLOAT64_BOX1, valueRange_d))
      m_valueRange = box1(float(valueRange_d[0]), float(valueRange_d[1]));

    m_colorData = getParamObject<Array1D>("color");
    m_color = float4(1.f);
    getParam("color", ANARI_FLOAT32_VEC3, &m_color);
    getParam("color", ANARI_FLOAT32_VEC4, &m_color);

    m_opacityData = getParamObject<Array1D>("opacity");
    m_opacity = getParam<float>("opacity", 1.f);

    m_unitDistance = getParam<float>("unitDistance", 1.f);
  }

  void finalize() override
  {
    if (!m_field) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "missing required parameter 'value' on 'transferFunction1D' volume");
    }
    if (!unitDistanceIsValid()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'unitDistance' (%g) on 'transferFunction1D' volume must be finite "
          "and above 0; the volume is invalid",
          m_unitDistance);
    }

    // any color element type converts like an attribute (e.g. float3 gets
    // alpha 1, UFIXED8 is normalized); helide only takes float3/float4
    m_colors = toAttributeValues(m_colorData.get());

    m_opacities.clear();
    if (m_opacityData && m_opacityData->elementType() != ANARI_FLOAT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'opacity' array elements are %s, but need to be %s; using the "
          "uniform opacity",
          anari::toString(m_opacityData->elementType()),
          anari::toString(ANARI_FLOAT32));
    } else if (m_opacityData) {
      m_opacities.assign(
          m_opacityData->beginAs<float>(), m_opacityData->endAs<float>());
    }

    ispc::Volume_setTransferFunction1D(getIE(),
        m_field ? m_field->getIE() : nullptr,
        m_valueRange.lower,
        m_valueRange.upper,
        m_unitDistance,
        &m_color.x,
        m_colors.empty() ? nullptr : &m_colors[0].x,
        m_colors.size(),
        m_opacity,
        m_opacities.empty() ? nullptr : m_opacities.data(),
        m_opacities.size(),
        id());

    // the pathtracer's majorant of each of the field's macrocells, in
    // chunks of cells on Embree's threads, from the field's value ranges.
    // helium finalizes fields before volumes; should the field change
    // after, it notifies this volume, and the commit buffer's repeated
    // flush (flushCommitBuffer()) finalizes it again before any render.
    const size_t cells =
        m_field ? ispc::SpatialField_numMacrocells(m_field->getIE()) : 0;
    m_macrocellMajorants.resize(cells);
    constexpr size_t CHUNK = 256;
    embree::parallel_for((cells + CHUNK - 1) / CHUNK, [&](size_t chunk) {
      ispc::Volume_computeMacrocellMajorants(getIE(),
          m_macrocellMajorants.data(),
          chunk * CHUNK,
          std::min(cells, (chunk + 1) * CHUNK));
    });
    ispc::Volume_setMacrocellMajorants(
        getIE(), m_macrocellMajorants.data(), cells);
  }

 private:
  // A distance of 0 or less has no extinction to give (sigma_t would be
  // infinite or negative), so neither renderer can draw the volume
  bool unitDistanceIsValid() const
  {
    return std::isfinite(m_unitDistance) && m_unitDistance > 0.f;
  }

  helium::ChangeObserverPtr<SpatialField> m_field{this};
  box1 m_valueRange{0.f, 1.f};
  float m_unitDistance{1.f};
  float4 m_color{1.f};
  float m_opacity{1.f};
  helium::ChangeObserverPtr<Array1D> m_colorData{this};
  helium::ChangeObserverPtr<Array1D> m_opacityData{this};
  // 'color'/'opacity' arrays as read by ISPC, empty if not set
  std::vector<float4> m_colors;
  std::vector<float> m_opacities;
  // as ISPC's Volume::macrocellMajorants reads them
  std::vector<float> m_macrocellMajorants;
};

// Unknown subtypes: never valid, so groups skip them
struct UnknownVolume : public Volume
{
  UnknownVolume(HalcyonDeviceGlobalState *s) : Volume(s) {}
  bool isValid() const override
  {
    return false;
  }
};

// Base Volume definitions ////////////////////////////////////////////////////

Volume::Volume(HalcyonDeviceGlobalState *s)
    : Object(ANARI_VOLUME, s, ispc::sizeof_Volume())
{
  // helium only finalizes objects with committed parameters
  ispc::Volume_setDefaults(getIE());
}

Volume *Volume::createInstance(
    std::string_view subtype, HalcyonDeviceGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  else
    return new UnknownVolume(s);
}

void Volume::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_visible = getParam<bool>("visible", true);
}

void Volume::markFinalized()
{
  Object::markFinalized();
  // groups pick valid, visible volumes when they rebuild
  deviceState()->objectUpdates.lastBLSReconstructSceneRequest =
      helium::newTimeStamp();
}

box3 Volume::bounds() const
{
  return box3();
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Volume *);
