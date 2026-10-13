// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Object.h"
#include "scene/AnariField.h"
#include "scene/Bounds.h"
// helium
#include "helium/array/Array3D.h"
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace anari_hydra {

// One published decode of a spatial field: an immutable grid registered in
// AnariFieldRegistry under `assetPath` for exactly as long as this snapshot
// lives. Stage 1 (finalize) publishes a new snapshot; the mirror holds the one
// it authored, so the asset path a field bprim names stays loadable until the
// mirror re-authors it, whenever stage 1 runs (SPEC §9.3). The mirror also
// publishes volume classification tables as snapshots (AnariFieldData::table),
// for which bounds() and proxyBounds() are meaningless.
struct FieldSnapshot
{
  FieldSnapshot(
      uint64_t revision, std::string assetPath, AnariFieldDataPtr grid);
  ~FieldSnapshot();
  FieldSnapshot(const FieldSnapshot &) = delete;
  FieldSnapshot &operator=(const FieldSnapshot &) = delete;

  // Object-space domain: [origin, origin + (dims - 1) * spacing].
  Bounds bounds() const;
  // The box Storm ray-marches (and rasterizes): the domain widened by half a
  // voxel, where its texel-centred field texture ends. Camera clipping must
  // contain it, or the proxy's back faces are clipped away.
  Bounds proxyBounds() const;

  const uint64_t revision;
  const std::string assetPath;
  const AnariFieldDataPtr grid; // never null
};

using FieldSnapshotPtr = std::shared_ptr<const FieldSnapshot>;

// ANARI spatial field -> a field bprim whose texture Storm loads from the
// in-memory field scheme (SPEC §7, AnariField.h). Every subtype decodes to one
// dense float32 vertex-centred grid (AnariFieldData) at commit, published as
// a FieldSnapshot under a revision-bumped `anari://field/…​.anari` path so
// a new commit is a new texture identity for Storm's texture cache.
//
// Implemented: structuredRegular (KHR_SPATIAL_FIELD_STRUCTURED_REGULAR) and,
// when built with the NanoVDB headers, nanovdb (KHR_SPATIAL_FIELD_NANOVDB,
// densified). VisRTX's rectilinear families are not implemented
// (README.md).
struct SpatialField : public Object
{
  static SpatialField *createInstance(
      std::string_view subtype, HydraGlobalState *s);
  // Whether this build implements `subtype` (nanovdb needs the NanoVDB
  // headers at build time).
  static bool supportsSubtype(std::string_view subtype);

  bool isValid() const override
  {
    return bool(snapshot());
  }

  uint64_t serial() const
  {
    return m_serial;
  }

  // The current decode, or null while invalid. Safe from any thread.
  FieldSnapshotPtr snapshot() const;

  // Domain / proxy of the current decode; empty while invalid.
  Bounds bounds() const;
  Bounds proxyBounds() const;

 protected:
  SpatialField(HydraGlobalState *s);

  // Publish `grid` (or nothing, when null) under a fresh revision and mark the
  // mirror. Subtypes call this from finalize().
  // Axes with negative spacing are reordered first (same domain and values).
  void publish(std::shared_ptr<AnariFieldData> grid);

  // Warn once per object that `filter` renders linear (Storm fixes the field
  // sampler); `linear` itself is silent.
  void checkFilter(const std::string &filter);

 private:
  uint64_t m_serial{0};
  uint64_t m_revision{0};
  mutable std::mutex m_mutex;
  FieldSnapshotPtr m_snapshot;
  bool m_warnedFilter{false};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::SpatialField *, ANARI_SPATIAL_FIELD);
}
