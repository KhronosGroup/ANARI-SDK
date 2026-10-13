// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "AnariField.h"

#include <pxr/base/gf/bbox3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/tf/type.h>
#include <pxr/imaging/hio/fieldTextureData.h>
#include <pxr/imaging/hio/types.h>

#include <algorithm>

namespace anari_hydra {

AnariFieldRegistry &AnariFieldRegistry::instance()
{
  static AnariFieldRegistry r;
  return r;
}

void AnariFieldRegistry::set(const std::string &path, AnariFieldDataPtr data)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fields[path] = std::move(data);
}

void AnariFieldRegistry::erase(const std::string &path)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_fields.erase(path);
}

AnariFieldDataPtr AnariFieldRegistry::get(const std::string &path) const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_fields.find(path);
  return it == m_fields.end() ? nullptr : it->second;
}

} // namespace anari_hydra

// --- the Hio field plugin --------------------------------------------------

// Like AnariHioImage, the class lives in the pxr namespace so its registered
// TfType name is exactly the plugInfo key ("AnariHioFieldTextureData").
PXR_NAMESPACE_OPEN_SCOPE

class AnariHioFieldTextureData final : public HioFieldTextureData
{
 public:
  using Base = HioFieldTextureData;

  explicit AnariHioFieldTextureData(const std::string &path)
      : _data(anari_hydra::AnariFieldRegistry::instance().get(path))
  {
    if (!_data)
      return;
    if (_data->table) {
      // See AnariFieldData::table: the range scaled down by S and the matrix
      // scaled up by S cover the same box, but Storm's step size (the
      // shortest matrix column) becomes S, never below the value field's.
      const GfVec3d lo(_data->boxLo), hi(_data->boxHi);
      const GfVec3d sz = hi - lo;
      const double S = std::max({sz[0], sz[1], sz[2], 1e-30});
      GfMatrix4d m(1.0);
      m.SetScale(GfVec3d(S));
      _bbox = GfBBox3d(GfRange3d(lo / S, hi / S), m);
      return;
    }
    // Index-space range widened by half a voxel so texel centres land on
    // the ANARI vertices; the matrix scales index space to object space.
    // Storm derives its ray-march step from this matrix (half the smallest
    // spacing, the same step VisRTX takes).
    const pxr::GfVec3i &n = _data->dims;
    const GfRange3d range(GfVec3d(-0.5), GfVec3d(n[0] - 0.5, n[1] - 0.5, n[2] - 0.5));
    GfMatrix4d m(1.0);
    m.SetScale(GfVec3d(_data->spacing));
    m.SetTranslateOnly(GfVec3d(_data->origin));
    _bbox = GfBBox3d(range, m);
  }

  const GfBBox3d &GetBoundingBox() const override { return _bbox; }
  // The grid is served at full resolution: Storm's per-field texture-memory
  // budget is not applied (README.md volume limitations).
  int ResizedWidth() const override { return _data ? _data->dims[0] : 0; }
  int ResizedHeight() const override { return _data ? _data->dims[1] : 0; }
  int ResizedDepth() const override { return _data ? _data->dims[2] : 0; }
  HioFormat GetFormat() const override { return HioFormatFloat32; }
  bool Read() override { return bool(_data); }
  bool HasRawBuffer() const override { return _data && !_data->values.empty(); }
  unsigned char const *GetRawBuffer() const override
  {
    return _data ? reinterpret_cast<unsigned char const *>(_data->values.data())
                 : nullptr;
  }

 private:
  anari_hydra::AnariFieldDataPtr _data;
  GfBBox3d _bbox;
};

class AnariHioFieldTextureDataFactory final
    : public HioFieldTextureDataFactoryBase
{
 protected:
  HioFieldTextureDataSharedPtr _New(std::string const &filePath,
      std::string const & /*fieldName*/,
      int /*fieldIndex*/,
      std::string const & /*fieldPurpose*/,
      size_t /*targetMemory*/) const override
  {
    return std::make_shared<AnariHioFieldTextureData>(filePath);
  }
};

TF_REGISTRY_FUNCTION(TfType)
{
  using T = AnariHioFieldTextureData;
  TfType t = TfType::Define<T, TfType::Bases<T::Base>>();
  t.SetFactory<AnariHioFieldTextureDataFactory>();
}

PXR_NAMESPACE_CLOSE_SCOPE
