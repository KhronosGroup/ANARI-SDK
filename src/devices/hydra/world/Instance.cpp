// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"

namespace anari_hydra {

// Slot order shared by the scalar/array attribute members: attribute0..3, color.
static const char *const kAttributeNames[5] = {
    "attribute0", "attribute1", "attribute2", "attribute3", "color"};

// Restore the implied (0,0,0,1) bottom row of a column-major 3x4 (four float3
// columns; the ANARI FLOAT32_MAT3x4 layout, glm's mat4x3).
static helium::mat4 ExpandMat3x4(const float *m)
{
  helium::mat4 r(linalg::identity);
  for (int c = 0; c < 4; ++c)
    r[c] = float4(m[3 * c], m[3 * c + 1], m[3 * c + 2], c == 3 ? 1.f : 0.f);
  return r;
}

Instance::Instance(HydraGlobalState *s)
    : Object(ANARI_INSTANCE, s),
      m_xfmArray(this),
      m_idArray(this),
      m_attrArray{{this}, {this}, {this}, {this}, {this}}
{
  m_serial = s->nextSerial++;
}

void Instance::commitParameters()
{
  m_group = getParamObject<Group>("group");

  m_idArray = getParamObject<helium::Array1D>("id");
  m_id = getParam<uint32_t>("id", ~0u);

  // Scalar transform: MAT4, or the compact MAT3x4. A parameter holds a single
  // typed value, so at most one of these is present.
  m_xfmArray = getParamObject<helium::Array1D>("transform");
  m_xfm = helium::mat4(linalg::identity);
  float compact[12];
  if (getParam("transform", ANARI_FLOAT32_MAT3x4, compact))
    m_xfm = ExpandMat3x4(compact);
  else
    m_xfm = getParam<helium::mat4>("transform", m_xfm);

  // Per-instance attribute overrides (SPEC §6.8). A scalar of any float width
  // is read into the ANARI default (0,0,0,1) so the missing lanes keep their
  // default; presence is tracked so an unset attribute stays out of the shared
  // instance-rate primvar array.
  m_overrides = InstanceOverrides{};
  for (int k = 0; k < 5; ++k) {
    const std::string name = kAttributeNames[k];
    m_attrArray[k] = getParamObject<helium::Array1D>(name);
    float4 v(0.f, 0.f, 0.f, 1.f);
    bool present = false;
    for (ANARIDataType t : {ANARI_FLOAT32, ANARI_FLOAT32_VEC2,
             ANARI_FLOAT32_VEC3, ANARI_FLOAT32_VEC4}) {
      if (getParam(name, t, &v)) {
        present = true;
        break;
      }
    }
    if (k == 4) {
      m_overrides.hasColor = present;
      m_overrides.color = v;
    } else {
      m_overrides.hasAttribute[k] = present;
      m_overrides.attribute[k] = v;
    }
  }
}

void Instance::finalize()
{
  if (m_xfmArray && m_xfmArray->elementType() != ANARI_FLOAT32_MAT4
      && m_xfmArray->elementType() != ANARI_FLOAT32_MAT3x4) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'transform' array elements are %s, but need to be %s or %s; ignoring",
        anari::toString(m_xfmArray->elementType()),
        anari::toString(ANARI_FLOAT32_MAT4),
        anari::toString(ANARI_FLOAT32_MAT3x4));
    m_xfmArray = nullptr;
  }
  if (m_idArray && m_idArray->elementType() != ANARI_UINT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'id' array elements are %s, but need to be %s; ignoring",
        anari::toString(m_idArray->elementType()),
        anari::toString(ANARI_UINT32));
    m_idArray = nullptr;
  }
  const size_t n = transformCount();
  if (m_idArray && m_xfmArray && m_idArray->size() < n) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'id' array has %zu elements for %zu transforms; missing elements "
        "fall back to the scalar 'id'",
        m_idArray->size(),
        n);
  }
  for (int k = 0; k < 5; ++k) {
    if (m_attrArray[k] && m_attrArray[k]->size() < n) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "'%s' array has %zu elements for %zu transforms; missing elements "
          "read as (0,0,0,1)",
          kAttributeNames[k],
          m_attrArray[k]->size(),
          n);
    }
  }
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");
  deviceState()->mirrorDirty.mark(this);
}

bool Instance::isValid() const
{
  return m_group;
}

size_t Instance::transformCount() const
{
  return m_xfmArray ? m_xfmArray->size() : 1;
}

helium::mat4 Instance::transform(size_t i) const
{
  if (!m_xfmArray)
    return m_xfm;
  if (m_xfmArray->elementType() == ANARI_FLOAT32_MAT3x4) {
    // helium's typed accessors reject a float view of a MAT3x4 array; read
    // the 12-float element through the untyped begin().
    return ExpandMat3x4(
        static_cast<const float *>(m_xfmArray->begin()) + 12 * i);
  }
  return m_xfmArray->beginAs<helium::mat4>()[i];
}

uint32_t Instance::id(size_t i) const
{
  // The id array is per-transform (KHR_INSTANCE_TRANSFORM_ARRAY), so it only
  // applies alongside a transform array — matching the schema and VisRTX.
  if (m_xfmArray && m_idArray && i < m_idArray->size())
    return m_idArray->beginAs<uint32_t>()[i];
  return m_id;
}

InstanceOverrides Instance::overrides(size_t i) const
{
  InstanceOverrides o = m_overrides;
  for (int k = 0; k < 5; ++k) {
    const helium::Array1D *arr = m_attrArray[k].get();
    if (!arr)
      continue;
    const float4 v = i < arr->size() ? arr->readAsAttributeValue(int32_t(i))
                                     : float4(0.f, 0.f, 0.f, 1.f);
    if (k == 4) {
      o.hasColor = true;
      o.color = v;
    } else {
      o.hasAttribute[k] = true;
      o.attribute[k] = v;
    }
  }
  return o;
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Instance *);
}
