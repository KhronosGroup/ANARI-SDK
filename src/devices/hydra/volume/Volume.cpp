// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"

#include <algorithm>
#include <cmath>

namespace anari_hydra {

Volume::Volume(HydraGlobalState *s) : Object(ANARI_VOLUME, s)
{
  m_serial = s->nextSerial++;
}

VolumeClassificationPtr Volume::classification() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_classification;
}

void Volume::publish(VolumeClassificationPtr c)
{
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_classification.swap(c);
  }
  deviceState()->mirrorDirty.mark(this);
}

bool Volume::isValid() const
{
  const VolumeClassificationPtr c = classification();
  return c && c->field && c->field->isValid();
}

Bounds Volume::bounds(bool proxy) const
{
  const VolumeClassificationPtr c = classification();
  if (!c || !c->field)
    return Bounds();
  return proxy ? c->field->proxyBounds() : c->field->bounds();
}

namespace {

// Resample `n` entries read by `at` to at most Volume::kMaxTableSize entries,
// linearly over the same [0,1] parameter, so the baked table keeps its shape.
template <typename T, typename Read>
static std::vector<T> BuildTable(size_t n, const Read &at)
{
  std::vector<T> out;
  if (n <= Volume::kMaxTableSize) {
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
      out.push_back(at(i));
    return out;
  }
  const size_t m = Volume::kMaxTableSize;
  out.reserve(m);
  for (size_t j = 0; j < m; ++j) {
    const double x = double(j) * double(n - 1) / double(m - 1);
    const size_t i = std::min(size_t(x), n - 2);
    const float f = float(x - double(i));
    out.push_back(at(i) * (1.f - f) + at(i + 1) * f);
  }
  return out;
}

// KHR_VOLUME_TRANSFER_FUNCTION1D. The field sample s maps to
// t = clamp((s - valueRange.lo) / (valueRange.hi - valueRange.lo), 0, 1);
// `color` and `opacity` (constants, or arrays spread uniformly over t) are
// linearly interpolated at t; final opacity = color.a * opacity (ANARI 1.1
// §5.12.1, as VisRTX). The shader turns opacity into extinction per
// unitDistance of object space (VolumeBuilder).
struct TransferFunction1D : public Volume
{
  TransferFunction1D(HydraGlobalState *s)
      : Volume(s), m_colorArray(this), m_opacityArray(this)
  {}

  void commitParameters() override
  {
    m_field = getParamObject<SpatialField>("value");
    m_visible = getParam<bool>("visible", true);
    m_unitDistance = getParam<float>("unitDistance", 1.f);

    m_valueRange[0] = 0.f;
    m_valueRange[1] = 1.f;
    double range64[2];
    if (!getParam("valueRange", ANARI_FLOAT32_BOX1, m_valueRange)
        && !getParam("valueRange", ANARI_FLOAT32_VEC2, m_valueRange)
        && getParam("valueRange", ANARI_FLOAT64_BOX1, range64)) {
      m_valueRange[0] = float(range64[0]);
      m_valueRange[1] = float(range64[1]);
    }

    m_colorArray = getParamObject<helium::Array1D>("color");
    m_uniformColor = helium::float4(1.f);
    helium::float3 c3;
    if (getParam("color", ANARI_FLOAT32_VEC3, &c3))
      m_uniformColor = helium::float4(c3.x, c3.y, c3.z, 1.f);
    getParam("color", ANARI_FLOAT32_VEC4, &m_uniformColor);

    m_opacityArray = getParamObject<helium::Array1D>("opacity");
    m_uniformOpacity = getParam<float>("opacity", 1.f);
  }

  void finalize() override
  {
    auto c = std::make_shared<VolumeClassification>();
    c->field = m_field;
    c->visible = m_visible;
    c->valueLo = m_valueRange[0];
    c->valueHi = m_valueRange[1];
    c->unitDistance = m_unitDistance;
    if (!m_field) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "transferFunction1D volume is missing its required 'value' "
          "parameter");
    }
    if (!(c->unitDistance > 0.f)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "transferFunction1D unitDistance must be positive; using 1e-6");
      c->unitDistance = 1e-6f;
    }

    const helium::Array1D *colors = m_colorArray.get();
    if (colors && colors->size() > 0) {
      c->colors = BuildTable<pxr::GfVec4f>(colors->size(), [&](size_t i) {
        const helium::float4 v = colors->readAsAttributeValue(int(i));
        return pxr::GfVec4f(v.x, v.y, v.z, v.w);
      });
    } else {
      const helium::float4 &v = m_uniformColor;
      c->colors = {pxr::GfVec4f(v.x, v.y, v.z, v.w)};
    }

    const helium::Array1D *opacities = m_opacityArray.get();
    if (opacities && opacities->size() > 0
        && opacities->elementType() == ANARI_FLOAT32) {
      const float *o = static_cast<const float *>(opacities->data());
      c->opacities =
          BuildTable<float>(opacities->size(), [&](size_t i) { return o[i]; });
    } else {
      if (opacities && opacities->elementType() != ANARI_FLOAT32) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "transferFunction1D opacity array must be FLOAT32; using the "
            "scalar opacity");
      }
      c->opacities = {m_uniformOpacity};
    }

    publish(std::move(c));
  }

 private:
  helium::ChangeObserverPtr<helium::Array1D> m_colorArray;
  helium::ChangeObserverPtr<helium::Array1D> m_opacityArray;
  helium::IntrusivePtr<SpatialField> m_field;
  helium::float4 m_uniformColor{1.f};
  float m_uniformOpacity{1.f};
  float m_valueRange[2]{0.f, 1.f};
  float m_unitDistance{1.f};
  bool m_visible{true};
};

} // namespace

Volume *Volume::createInstance(std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  return nullptr;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Volume *);
}
