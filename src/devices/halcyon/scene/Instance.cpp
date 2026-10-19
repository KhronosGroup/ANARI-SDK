// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Instance.h"
#include "Instance_ispc.h"

namespace halcyon {

Instance::Instance(HalcyonDeviceGlobalState *s)
    : Object(ANARI_INSTANCE, s, ispc::sizeof_Instance()),
      m_xfmArray(this),
      m_idArray(this),
      m_uniformAttrArrays{{{this}, {this}, {this}, {this}, {this}}}
{
  m_embreeGeometry =
      rtcNewGeometry(s->embreeDevice, RTC_GEOMETRY_TYPE_INSTANCE_ARRAY);
}

Instance::~Instance()
{
  rtcReleaseGeometry(m_embreeGeometry);
}

void Instance::commitParameters()
{
  m_idArray = getParamObject<Array1D>("id");
  m_id = getParam<uint32_t>("id", ~0u);
  m_xfmArray = getParamObject<Array1D>("transform");
  m_xfm = getParam<mat4>("transform", mat4(linalg::identity));
  m_group = getParamObject<Group>("group");

  for (int i = 0; i < NUM_ATTRIBUTES; i++) {
    float4 attrV = helium::DEFAULT_ATTRIBUTE_VALUE;
    m_uniformAttr[i].reset();
    if (getParam(ATTRIBUTE_NAMES[i], ANARI_FLOAT32_VEC4, &attrV))
      m_uniformAttr[i] = attrV;
  }

  for (int i = 0; i < NUM_ATTRIBUTES; i++)
    m_uniformAttrArrays[i] = getParamObject<Array1D>(ATTRIBUTE_NAMES[i]);
}

void Instance::finalize()
{
  m_invXfmData.clear();
  if (m_idArray && m_idArray->elementType() != ANARI_UINT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'id' array elements are %s, but need to be %s",
        anari::toString(m_idArray->elementType()),
        anari::toString(ANARI_UINT32));
    m_idArray = {};
  }
  if (m_xfmArray && m_xfmArray->elementType() != ANARI_FLOAT32_MAT4) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'transform' array elements are %s, but need to be %s",
        anari::toString(m_xfmArray->elementType()),
        anari::toString(ANARI_FLOAT32_MAT4));
    m_xfmArray = {};
  } else if (m_xfmArray) {
    m_invXfmData.resize(m_xfmArray->totalSize());
    std::transform(m_xfmArray->beginAs<mat4>(),
        m_xfmArray->endAs<mat4>(),
        m_invXfmData.begin(),
        [](const mat4 &m) { return linalg::inverse(m); });
  }
  if (m_xfmArray && m_idArray && m_idArray->totalSize() < numTransforms()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "'id' array has %zu elements, but 'transform' array has %u",
        m_idArray->totalSize(),
        numTransforms());
    m_idArray = {};
  }
  if (!m_group)
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'group' on ANARIInstance");

  m_invXfm = linalg::inverse(m_xfm);
  if (m_invXfmData.empty())
    m_invXfmData.push_back(m_invXfm);

  // Embree reports Ng in object space for instanced hits, so shading needs
  // each transform's normal matrix (the inverse transpose of its linear part)
  m_normalXfmData.resize(numTransforms());
  m_ids.resize(numTransforms());
  for (auto &a : m_attrData)
    a.clear();
  for (uint32_t i = 0; i < numTransforms(); i++) {
    m_normalXfmData[i] = normalXfm(i);
    m_ids[i] = id(i);
    const auto attrs = getUniformAttributes(i);
    for (int a = 0; a < NUM_ATTRIBUTES; a++) {
      if (attrs[a])
        m_attrData[a].push_back(*attrs[a]);
    }
  }

  ispc::Instance_setGroup(getIE(), m_group ? m_group->getIE() : nullptr);
  ispc::Instance_setNumTransforms(getIE(), numTransforms());
  // ISPC reads these as packed floats (load_linear3f(), load_affine3f())
  static_assert(sizeof(mat3) == 9 * sizeof(float));
  static_assert(sizeof(mat4) == 16 * sizeof(float));
  ispc::Instance_setNormalXfms(
      getIE(), reinterpret_cast<const float *>(m_normalXfmData.data()));
  ispc::Instance_setIds(getIE(), m_ids.data());
  // copied, so they don't depend on the 'transform' array's memory
  m_xfmData.resize(numTransforms());
  for (uint32_t i = 0; i < numTransforms(); i++)
    m_xfmData[i] = xfm(i);
  ispc::Instance_setXfms(getIE(),
      reinterpret_cast<const float *>(m_xfmData.data()),
      reinterpret_cast<const float *>(m_invXfmData.data()));
  for (int a = 0; a < NUM_ATTRIBUTES; a++) {
    ispc::Instance_setAttribute(getIE(),
        a,
        m_attrData[a].empty()
            ? nullptr
            : reinterpret_cast<const float *>(m_attrData[a].data()));
  }
}

void Instance::markFinalized()
{
  Object::markFinalized();
  deviceState()->objectUpdates.lastTLSReconstructSceneRequest =
      helium::newTimeStamp();
}

bool Instance::isValid() const
{
  return m_group;
}

uint32_t Instance::numTransforms() const
{
  return m_xfmArray ? uint32_t(m_xfmArray->totalSize()) : 1u;
}

uint32_t Instance::id(uint32_t i) const
{
  return m_xfmArray && m_idArray ? m_idArray->beginAs<uint32_t>()[i] : m_id;
}

const mat4 &Instance::xfm(uint32_t i) const
{
  return m_xfmArray ? m_xfmArray->beginAs<mat4>()[i] : m_xfm;
}

const mat4 &Instance::invXfm(uint32_t i) const
{
  return m_invXfmData.empty() ? m_invXfm : m_invXfmData[i];
}

mat3 Instance::normalXfm(uint32_t i) const
{
  // the inverse's linear part is the linear part's inverse (for the affine
  // transforms Embree uses), so this needs no second inversion
  return linalg::transpose(extractRotation(invXfm(i)));
}

UniformAttributeSet Instance::getUniformAttributes(uint32_t i) const
{
  UniformAttributeSet retval = m_uniformAttr;

  for (int a = 0; a < NUM_ATTRIBUTES; a++) {
    if (m_uniformAttrArrays[a])
      retval[a] = attributeValueAt(*m_uniformAttrArrays[a], i);
  }

  return retval;
}

const Group *Instance::group() const
{
  return m_group.ptr;
}

Group *Instance::group()
{
  return m_group.ptr;
}

RTCGeometry Instance::embreeGeometry() const
{
  return m_embreeGeometry;
}

void Instance::embreeGeometryUpdate()
{
  rtcSetGeometryInstancedScene(m_embreeGeometry, group()->embreeScene());
  auto *xfms = rtcSetNewGeometryBuffer(m_embreeGeometry,
      RTC_BUFFER_TYPE_TRANSFORM,
      0,
      RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
      sizeof(mat4),
      this->numTransforms());
  std::memcpy(xfms,
      m_xfmArray ? m_xfmArray->begin() : &m_xfm,
      this->numTransforms() * sizeof(mat4));
  rtcCommitGeometry(m_embreeGeometry);
}

} // namespace halcyon

HALCYON_ANARI_TYPEFOR_DEFINITION(halcyon::Instance *);
