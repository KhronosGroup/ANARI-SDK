// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// One retained mirror per ANARI world (SPEC §4.4), rooted /anari/w{N}/. Built
// from zero-copy array wrappers; mutated only via AddPrims / DirtyPrims
// (locator-precise) / RemovePrims from the render prologue (stage 2, SPEC §9.3).

#pragma once

#include "InstancerBuilder.h"
#include "PrimBuilder.h"
#include "world/Instance.h"
#include "spatial_field/SpatialField.h"
#include "volume/Volume.h"

#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace anari_hydra {

struct World;
struct Surface;
struct Material;
struct Light;

struct PickingIds
{
  uint32_t object{~0u};
  std::vector<uint32_t> primitives;
  std::vector<uint32_t> instances;
  std::vector<uint32_t> triangles;
  // A per-instance copy of an instanced surface (SPEC §6.4): not instanced in
  // Hydra, so every hit belongs to the one instance in `instances`.
  bool perInstanceCopy{false};
};

class Mirror
{
 public:
  explicit Mirror(uint64_t worldSerial);

  pxr::HdRetainedSceneIndexRefPtr sceneIndex() const { return m_scene; }
  const pxr::SdfPath &root() const { return m_root; }

  // Reconcile the retained scene index with the world's committed state. Runs
  // only on the worker thread, as the render prologue (stage 2).
  void sync(World *world);
  const std::map<pxr::SdfPath, PickingIds> &pickingIds() const
  {
    return m_pickingIds;
  }

 private:
  struct PrimRecord
  {
    Surface *surface{nullptr};
    Material *material{nullptr};
    const void *pointsPtr{nullptr};
    size_t pointsCount{0};
    const void *indexPtr{nullptr};
    size_t indexCount{0};
    // App-provided vertex.normal binding (mesh only). Authored as a decode-copy
    // `normals` primvar, not zero-copy, so any change to it forces a full
    // rebuild rather than the points fast path (see incremental below).
    const void *normalsPtr{nullptr};
    size_t normalsCount{0};
    pxr::GfMatrix4d xform{1};
    pxr::GfVec3f color{0.8f, 0.8f, 0.8f};
    float opacity{1.f};
    // True when the rprim's only mutable bulk data is zero-copy points, so an
    // in-place mutation can take the DirtyPrims(points) fast path (SPEC §5.2).
    bool incremental{false};
    // Non-empty for a prototype rprim of an instancer (SPEC §4.3): the rprim
    // carries an `instancedBy` pointing at this instancer / prototype-root.
    pxr::SdfPath instancerPath;
    pxr::SdfPath prototypeRoot;
    uint32_t instanceConstantMask{0};
    // Slots some instance of the group overrides (InstanceBinding).
    uint32_t instanceOverrideMask{0};
    // Set for a per-instance copy (SPEC §6.4): that instance's overrides.
    std::optional<InstanceOverrides> instanceValues;
    // Signature (serial:revision tuples) of the material's samplers that bake
    // a derived primvar on this rprim (SPEC §6.4: baked image coordinates,
    // transform and primitive sampler values), so a sampler edit re-authors
    // the copy.
    std::string derivedSig;
  };

  // Tracks an instancer's prototype rprim paths + per-instance transforms so an
  // unchanged instancer is not needlessly re-added (SPEC §4.3). `attributes`
  // holds the per-instance override primvars (instance.color / attribute0..3,
  // SPEC §6.8) as (primvar-name, instance-rate array) pairs; the VtValue arrays
  // compare by value so an override change re-authors the instancer.
  struct InstancerRecord
  {
    std::vector<pxr::SdfPath> prototypes;
    std::vector<pxr::GfMatrix4d> transforms;
    std::vector<InstancerAttribute> attributes;
  };

  // Tracks a material sprim's authored parameters so an unchanged material is
  // not needlessly re-added (SPEC §6.1).
  struct MatRecord
  {
    Material *material{nullptr};
    pxr::GfVec3f color{0.8f, 0.8f, 0.8f};
    pxr::GfVec3f emissive{0.f, 0.f, 0.f};
    float opacity{1.f};
    float metallic{0.f};
    float roughness{1.f};
    float ior{1.5f};
    float clearcoat{0.f};
    float clearcoatRoughness{0.f};
    float occlusion{1.f};
    int alphaMode{0};
    float opacityThreshold{0.f};
    // Signature of this material's sampler bindings (input:samplerSerial:revision tuples,
    // SPEC §6.1) so binding/unbinding a sampler forces a network rebuild.
    std::string samplerSig;
    // Signature of this material's attribute bindings (input:primvar pairs,
    // SPEC §6.1) so binding/unbinding an attribute forces a network rebuild.
    std::string attributeSig;
  };

  static PrimRecord MakeRecord(Surface *s, const pxr::GfMatrix4d &xform);
  // The record's instancing, as BuildSurfacePrim takes it.
  static InstanceBinding Binding(const PrimRecord &rec);
  static MatRecord MakeMatRecord(Material *m);
  static bool SameMaterial(const MatRecord &a, const MatRecord &b);
  // Material-sprim path for a material in this world's mirror (SPEC §4.2).
  pxr::SdfPath materialPath(Material *m) const;
  // Reconcile material sprims referenced by `desired` rprims.
  void syncMaterials(const std::map<pxr::SdfPath, PrimRecord> &desired);
  // Reconcile instancer prims; adds/removes/rebuilds against transform changes.
  void syncInstancers(
      const std::map<pxr::SdfPath, InstancerRecord> &desiredInstancers);
  static bool SameInstancer(const InstancerRecord &a, const InstancerRecord &b);
  // Light-sprim path for a world-direct light: /anari/w{N}/lights/l{L} (§4.2).
  pxr::SdfPath lightPath(Light *l) const;
  // Expanded light-sprim path for an (instance, light) pair:
  // /anari/w{N}/lights/i{I}_l{L} (SPEC §6.8); element `k` of an
  // array-transform instance goes to /anari/w{N}/lights/i{I}_t{k}_l{L}.
  pxr::SdfPath expandedLightPath(
      uint64_t instanceSerial, int element, uint64_t lightSerial) const;
  // Reconcile light sprims: world-direct plus instanced-group expansion
  // (add/remove/re-author on any commit, SPEC §6.6/§6.8).
  void syncLights(World *world);
  // True when a prim's structure is unchanged (same buffer pointer, counts,
  // xform, and material), so the only thing that could have changed is bulk
  // vertex content mutated in place — making the update a locator-precise
  // DirtyPrims(points) rather than a resync. Any pointer/topology/xform/color
  // change instead forces a rebuild.
  static bool SamePrimTopology(const PrimRecord &a, const PrimRecord &b);

  // A volume rprim (SPEC §7): world-direct or a prototype of its group's
  // instancer, with the committed states it was authored from. Holding the
  // field snapshot keeps its asset registered while the field bprim names it.
  // A new snapshot (any commit) re-authors the field, material and rprim.
  struct VolumeRecord
  {
    VolumeClassificationPtr volume;
    FieldSnapshotPtr field;
    pxr::SdfPath instancerPath;
    pxr::SdfPath prototypeRoot;
    bool operator==(const VolumeRecord &o) const
    {
      return volume == o.volume && field == o.field
          && instancerPath == o.instancerPath
          && prototypeRoot == o.prototypeRoot;
    }
  };
  pxr::SdfPath fieldPath(const SpatialField *f) const;
  pxr::SdfPath volumeMaterialPath() const;
  pxr::SdfPath volumeTablePath(const Volume *v) const;
  // Reconcile volume rprims and their field bprims / material sprims.
  // `desired` maps rprim path -> (volume, record).
  void syncVolumes(
      const std::map<pxr::SdfPath, std::pair<Volume *, VolumeRecord>> &desired);

  std::map<pxr::SdfPath, VolumeRecord> m_presentVolumes;
  // Field bprims -> the snapshots they were authored at.
  std::map<pxr::SdfPath, FieldSnapshotPtr> m_presentFields;
  // Classification table bprims -> the classification and field they were
  // built from, and the published table (held so its asset stays loadable).
  struct VolumeTableRecord
  {
    VolumeClassificationPtr volume;
    FieldSnapshotPtr field;
    FieldSnapshotPtr table;
  };
  std::map<pxr::SdfPath, VolumeTableRecord> m_presentVolumeTables;
  // The one volume material (BuildVolumeMaterialPrim), authored while any
  // volume is present.
  bool m_hasVolumeMaterial{false};

  std::map<pxr::SdfPath, PickingIds> m_pickingIds;
  pxr::HdRetainedSceneIndexRefPtr m_scene;
  pxr::SdfPath m_root;
  std::map<pxr::SdfPath, PrimRecord> m_present;
  std::map<pxr::SdfPath, MatRecord> m_presentMaterials;
  std::map<pxr::SdfPath, InstancerRecord> m_presentInstancers;
  std::vector<pxr::SdfPath> m_presentLights;
};

} // namespace anari_hydra
