// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"

// helium
#include "helium/array/Array1D.h"
// anari
#include <anari/frontend/type_utility.h>
#ifdef ANARI_HYDRA_HAS_NANOVDB
#include <nanovdb/NanoVDB.h>
#endif
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace anari_hydra {

// Snapshot ///////////////////////////////////////////////////////////////////

FieldSnapshot::FieldSnapshot(
    uint64_t revision, std::string path, AnariFieldDataPtr data)
    : revision(revision), assetPath(std::move(path)), grid(std::move(data))
{
  AnariFieldRegistry::instance().set(assetPath, grid);
}

FieldSnapshot::~FieldSnapshot()
{
  AnariFieldRegistry::instance().erase(assetPath);
}

Bounds FieldSnapshot::bounds() const
{
  const AnariFieldData &g = *grid;
  pxr::GfVec3f hi;
  for (int k = 0; k < 3; ++k)
    hi[k] = g.origin[k] + float(g.dims[k] - 1) * g.spacing[k];
  return Bounds(g.origin, hi); // spacing is positive (NormalizeAxes)
}

Bounds FieldSnapshot::proxyBounds() const
{
  const Bounds b = bounds();
  const pxr::GfVec3f half(std::fabs(grid->spacing[0]) * 0.5f,
      std::fabs(grid->spacing[1]) * 0.5f,
      std::fabs(grid->spacing[2]) * 0.5f);
  return Bounds(b.GetMin() - half, b.GetMax() + half);
}

// Reorder any axis with negative spacing so every spacing is positive and
// `origin` is the min corner: the same vertices, values and domain, in the
// orientation Storm's texel-centred field box (and the shader's domain test)
// assume.
static void NormalizeAxes(AnariFieldData &g)
{
  for (int k = 0; k < 3; ++k) {
    if (g.spacing[k] >= 0.f)
      continue;
    const int n = g.dims[k];
    g.origin[k] += float(n - 1) * g.spacing[k];
    g.spacing[k] = -g.spacing[k];
    const size_t stride = k == 0
        ? 1
        : (k == 1 ? size_t(g.dims[0]) : size_t(g.dims[0]) * g.dims[1]);
    const size_t outer = g.values.size() / (stride * n);
    for (size_t o = 0; o < outer; ++o) {
      for (size_t in = 0; in < stride; ++in) {
        float *base = g.values.data() + o * stride * n + in;
        for (int a = 0, b = n - 1; a < b; ++a, --b)
          std::swap(base[size_t(a) * stride], base[size_t(b) * stride]);
      }
    }
  }
}

// Base ///////////////////////////////////////////////////////////////////////

SpatialField::SpatialField(HydraGlobalState *s) : Object(ANARI_SPATIAL_FIELD, s)
{
  m_serial = s->nextSerial++;
}

FieldSnapshotPtr SpatialField::snapshot() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_snapshot;
}

Bounds SpatialField::bounds() const
{
  const FieldSnapshotPtr snap = snapshot();
  return snap ? snap->bounds() : Bounds();
}

Bounds SpatialField::proxyBounds() const
{
  const FieldSnapshotPtr snap = snapshot();
  return snap ? snap->proxyBounds() : Bounds();
}

void SpatialField::publish(std::shared_ptr<AnariFieldData> grid)
{
  if (grid)
    NormalizeAxes(*grid);
  // A new asset identity per revision invalidates Storm's path-keyed texture
  // cache (the sampler image scheme's cache-bust). The previous snapshot stays
  // registered while the mirror still holds it.
  ++m_revision;
  FieldSnapshotPtr next;
  if (grid) {
    next = std::make_shared<const FieldSnapshot>(m_revision,
        "anari://field/f" + std::to_string(m_serial) + "_r"
            + std::to_string(m_revision) + ".anari",
        std::move(grid));
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot.swap(next);
  }
  deviceState()->mirrorDirty.mark(this);
}

void SpatialField::checkFilter(const std::string &filter)
{
  if (filter == "linear" || m_warnedFilter)
    return;
  // Storm's field textures are always sampled trilinearly (HdSt_VolumeShader
  // fixes the sampler), README.md.
  reportMessage(ANARI_SEVERITY_WARNING,
      "spatial field filter '%s' renders as 'linear' on Hydra field textures",
      filter.c_str());
  m_warnedFilter = true;
}

// structuredRegular //////////////////////////////////////////////////////////

namespace {

// Element value -> float with ANARI's normalized fixed-point conventions
// (UFIXEDn to [0,1], FIXEDn to [-1,1]); the same reads VisRTX's normalized
// texture fetch produces.
template <typename T>
static void decodeAs(const void *src, size_t n, float scale, float *dst)
{
  const T *s = static_cast<const T *>(src);
  for (size_t i = 0; i < n; ++i)
    dst[i] = float(s[i]) * scale;
}

static bool decodeScalars(
    ANARIDataType type, const void *src, size_t n, float *dst)
{
  switch (type) {
  case ANARI_UFIXED8:
    decodeAs<uint8_t>(src, n, 1.f / 255.f, dst);
    return true;
  case ANARI_UFIXED16:
    decodeAs<uint16_t>(src, n, 1.f / 65535.f, dst);
    return true;
  case ANARI_FIXED8:
    decodeAs<int8_t>(src, n, 1.f / 127.f, dst);
    break;
  case ANARI_FIXED16:
    decodeAs<int16_t>(src, n, 1.f / 32767.f, dst);
    break;
  case ANARI_FLOAT32:
    decodeAs<float>(src, n, 1.f, dst);
    return true;
  case ANARI_FLOAT64:
    decodeAs<double>(src, n, 1.f, dst);
    return true;
  default:
    return false;
  }
  // Only the signed fixed-point cases reach here: the most negative code
  // clamps to -1.
  for (size_t i = 0; i < n; ++i)
    dst[i] = std::max(dst[i], -1.f);
  return true;
}

// KHR_SPATIAL_FIELD_STRUCTURED_REGULAR: a vertex-centred scalar Array3D at
// origin + index * spacing. Decoded to float32 (copy-at-load: 4 bytes per
// voxel held in the registry for the field's lifetime, plus Storm's float32
// 3D texture on the GPU).
struct StructuredRegular : public SpatialField
{
  StructuredRegular(HydraGlobalState *s) : SpatialField(s), m_data(this) {}

  void commitParameters() override
  {
    m_data = getParamObject<helium::Array3D>("data");
    m_origin = getParam<helium::float3>("origin", helium::float3(0.f));
    m_spacing = getParam<helium::float3>("spacing", helium::float3(1.f));
    m_filter = getParamString("filter", "linear");
  }

  void finalize() override
  {
    // Array observers schedule finalization without re-reading parameters,
    // so an in-place unmap re-decodes here too.
    publish(decode());
  }

 private:
  std::shared_ptr<AnariFieldData> decode()
  {
    helium::Array3D *a = m_data.get();
    if (!a) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "structuredRegular spatial field is missing its required 'data' "
          "parameter");
      return nullptr;
    }
    const auto dims = a->size();
    if (dims.x == 0 || dims.y == 0 || dims.z == 0)
      return nullptr;
    for (int k = 0; k < 3; ++k) {
      if (!(m_spacing[k] != 0.f) || !std::isfinite(m_spacing[k])) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "structuredRegular spacing must be finite and nonzero");
        return nullptr;
      }
    }
    checkFilter(m_filter);
    auto grid = std::make_shared<AnariFieldData>();
    grid->dims = pxr::GfVec3i(int(dims.x), int(dims.y), int(dims.z));
    grid->origin = pxr::GfVec3f(m_origin.x, m_origin.y, m_origin.z);
    grid->spacing = pxr::GfVec3f(m_spacing.x, m_spacing.y, m_spacing.z);
    grid->values.resize(a->totalSize());
    if (!decodeScalars(
            a->elementType(), a->data(), a->totalSize(), grid->values.data())) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "structuredRegular data has unsupported element type %s",
          anari::toString(a->elementType()));
      return nullptr;
    }
    return grid;
  }

  helium::ChangeObserverPtr<helium::Array3D> m_data;
  helium::float3 m_origin{0.f};
  helium::float3 m_spacing{1.f};
  std::string m_filter{"linear"};
};

#ifdef ANARI_HYDRA_HAS_NANOVDB

// Densify a NanoVDB grid's active index box into the vertex-centred float
// grid: NanoVDB values sit at integer index coordinates and its trilinear
// sampler interpolates between them, the structuredRegular convention. The
// box gains one voxel layer on its max side so the domain is the grid's
// worldBBox (index [min, max + 1], as VisRTX bounds it); every value, in the
// box or that layer, is the accessor's (active value, tile value, or
// background), so sampling matches NanoVDB's own trilinear sampler exactly.
template <typename BuildT>
static std::shared_ptr<AnariFieldData> DensifyNanoVdb(
    const nanovdb::GridData *data)
{
  const auto *grid = reinterpret_cast<const nanovdb::NanoGrid<BuildT> *>(data);
  const nanovdb::CoordBBox box = grid->indexBBox();
  if (box.empty())
    return nullptr;
  const nanovdb::Coord lo = box.min();
  const nanovdb::Coord dim = box.dim();
  auto out = std::make_shared<AnariFieldData>();
  out->dims = pxr::GfVec3i(dim[0] + 1, dim[1] + 1, dim[2] + 1);
  const nanovdb::Vec3d o =
      grid->indexToWorld(nanovdb::Vec3d(lo[0], lo[1], lo[2]));
  out->origin = pxr::GfVec3f(float(o[0]), float(o[1]), float(o[2]));
  const nanovdb::Map &map = grid->map();
  out->spacing = pxr::GfVec3f(
      float(map.mMatD[0]), float(map.mMatD[4]), float(map.mMatD[8]));
  out->values.resize(size_t(out->dims[0]) * out->dims[1] * out->dims[2]);
  auto acc = grid->getAccessor();
  size_t i = 0;
  for (int z = 0; z < out->dims[2]; ++z)
    for (int y = 0; y < out->dims[1]; ++y)
      for (int x = 0; x < out->dims[0]; ++x)
        out->values[i++] = float(
            acc.getValue(nanovdb::Coord(lo[0] + x, lo[1] + y, lo[2] + z)));
  return out;
}

// KHR_SPATIAL_FIELD_NANOVDB: a NanoVDB grid buffer in a UINT8 Array1D. Scalar
// grid types (Float, Fp4/Fp8/Fp16/FpN) with an axis-aligned index-to-world
// map are densified (DensifyNanoVdb); the sparse topology is not preserved,
// so memory is 4 bytes per voxel of the (active box + 1)^3 on the CPU and
// again on the GPU (README.md).
struct NanoVdb : public SpatialField
{
  NanoVdb(HydraGlobalState *s) : SpatialField(s), m_data(this) {}

  void commitParameters() override
  {
    m_data = getParamObject<helium::Array1D>("data");
    m_filter = getParamString("filter", "linear");
  }

  void finalize() override
  {
    publish(decode());
  }

 private:
  std::shared_ptr<AnariFieldData> decode()
  {
    helium::Array1D *a = m_data.get();
    if (!a || a->elementType() != ANARI_UINT8
        || a->size() < sizeof(nanovdb::GridData)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "nanovdb spatial field needs a UINT8 'data' array holding a grid");
      return nullptr;
    }
    checkFilter(m_filter);
    // The grid header requires 32-byte alignment; app memory need not have
    // it, so read from an aligned copy (freed once densified).
    const size_t bytes = a->size();
    const size_t alignedBytes = (bytes + NANOVDB_DATA_ALIGNMENT - 1)
        / NANOVDB_DATA_ALIGNMENT * NANOVDB_DATA_ALIGNMENT;
    std::unique_ptr<void, decltype(&std::free)> copy(
        std::aligned_alloc(NANOVDB_DATA_ALIGNMENT, alignedBytes), &std::free);
    if (!copy)
      return nullptr;
    std::memcpy(copy.get(), a->data(), bytes);
    const auto *data = static_cast<const nanovdb::GridData *>(copy.get());
    if (!data->isValid() || data->mGridSize > bytes) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "nanovdb spatial field data is not a valid NanoVDB grid");
      return nullptr;
    }
    const nanovdb::Map &map = data->mMap;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        if (r != c && map.mMatD[r * 3 + c] != 0.0) {
          reportMessage(ANARI_SEVERITY_WARNING,
              "nanovdb grids with a rotated or sheared index-to-world map "
              "are not supported");
          return nullptr;
        }
    switch (data->mGridType) {
    case nanovdb::GridType::Float:
      return DensifyNanoVdb<float>(data);
    case nanovdb::GridType::Fp4:
      return DensifyNanoVdb<nanovdb::Fp4>(data);
    case nanovdb::GridType::Fp8:
      return DensifyNanoVdb<nanovdb::Fp8>(data);
    case nanovdb::GridType::Fp16:
      return DensifyNanoVdb<nanovdb::Fp16>(data);
    case nanovdb::GridType::FpN:
      return DensifyNanoVdb<nanovdb::FpN>(data);
    default:
      reportMessage(ANARI_SEVERITY_WARNING,
          "nanovdb spatial field supports scalar Float/Fp4/Fp8/Fp16/FpN "
          "grids only");
      return nullptr;
    }
  }

  helium::ChangeObserverPtr<helium::Array1D> m_data;
  std::string m_filter{"linear"};
};

#endif // ANARI_HYDRA_HAS_NANOVDB

} // namespace

SpatialField *SpatialField::createInstance(
    std::string_view subtype, HydraGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegular(s);
#ifdef ANARI_HYDRA_HAS_NANOVDB
  if (subtype == "nanovdb")
    return new NanoVdb(s);
#endif
  return nullptr;
}

bool SpatialField::supportsSubtype(std::string_view subtype)
{
#ifdef ANARI_HYDRA_HAS_NANOVDB
  if (subtype == "nanovdb")
    return true;
#endif
  return subtype == "structuredRegular";
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::SpatialField *);
}
