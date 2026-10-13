// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Mirror.h"
#include "InstancerBuilder.h"
#include "LightBuilder.h"
#include "MaterialBuilder.h"
#include "PrimBuilder.h"
#include "VolumeBuilder.h"
#include "geometry/Geometry.h"
#include "light/Light.h"
#include "material/Material.h"
#include "spatial_field/SpatialField.h"
#include "volume/Volume.h"
#include "world/Group.h"
#include "world/World.h"

#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/tokens.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/array.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// linalg column-major (M*v) -> GfMatrix4d row-vector (v*G): G[i][j] = L col i,
// component j (see derivation in commit notes).
static GfMatrix4d ToGfMatrix(const helium::mat4 &m)
{
  GfMatrix4d g;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      g[i][j] = m[i][j];
  return g;
}

// Per-instance attribute overrides -> instance-rate primvar arrays (SPEC §6.8),
// read per instance by the prototype materials' UsdPrimvarReader nodes (§6.1).
// One array covers every instance in the group, so an attribute is authored only
// when at least one instance set it; instances that did not fall to the ANARI
// default attribute value (0,0,0,1). This fill is safe because a prototype's own
// per-vertex primvar wins over the inherited instance-rate one (ANARI precedence
// per-vertex > per-instance-uniform, proven in prototype 014) — the filled
// default never clobbers a real per-vertex value. The residual gap is a mixed
// group with no per-vertex primvar where only some instances set the attribute:
// the unset instances render the default rather than the reader's own fallback.
// `color` is authored float3 to match the float3 `color` per-vertex primvar and
// color-slot reader; `attribute0..3` keep the ANARI float4 attribute type.
static std::vector<InstancerAttribute> BuildInstanceAttributes(
    const std::vector<InstanceOverrides> &overrides)
{
  const size_t n = overrides.size();
  std::vector<InstancerAttribute> out;

  bool anyColor = false;
  bool anyAttr[4] = {false, false, false, false};
  for (const auto &o : overrides) {
    anyColor = anyColor || o.hasColor;
    for (int k = 0; k < 4; ++k)
      anyAttr[k] = anyAttr[k] || o.hasAttribute[k];
  }

  if (anyColor) {
    VtVec3fArray colors(n);
    for (size_t i = 0; i < n; ++i) {
      const float4 &c = overrides[i].color;   // default (0,0,0,1)
      colors[i] = GfVec3f(c.x, c.y, c.z);
    }
    VtFloatArray scalar(n);
    VtVec2fArray uv(n);
    for (size_t i = 0; i < n; ++i) {
      scalar[i] = colors[i][0];
      uv[i] = GfVec2f(colors[i][0], colors[i][1]);
    }
    out.emplace_back(TfToken("color_float1"), VtValue(scalar));
    out.emplace_back(TfToken("color_float2"), VtValue(uv));
    out.emplace_back(TfToken("color_float3"), VtValue(colors));
    out.emplace_back(TfToken("color"), VtValue(colors));
  }
  for (int k = 0; k < 4; ++k) {
    if (!anyAttr[k])
      continue;
    VtVec4fArray vals(n);
    for (size_t i = 0; i < n; ++i) {
      const float4 &a = overrides[i].attribute[k];   // default (0,0,0,1)
      vals[i] = GfVec4f(a.x, a.y, a.z, a.w);
    }
    VtFloatArray scalar(n);
    VtVec3fArray color(n);
    for (size_t i = 0; i < n; ++i) {
      scalar[i] = vals[i][0];
      color[i] = GfVec3f(vals[i][0], vals[i][1], vals[i][2]);
    }
    out.emplace_back(TfToken(AttributePrimvarName(k) + "_float1"), VtValue(scalar));
    out.emplace_back(TfToken(AttributePrimvarName(k) + "_float3"), VtValue(color));
    out.emplace_back(TfToken(AttributePrimvarName(k)), VtValue(vals));
    // The float2 form sampler readers take (SPEC §6.4): `st` for attribute0.
    VtVec2fArray st(n);
    for (size_t i = 0; i < n; ++i)
      st[i] = GfVec2f(vals[i][0], vals[i][1]);
    out.emplace_back(
        TfToken(k == 0 ? "st" : AttributePrimvarName(k) + "_float2"), VtValue(st));
  }
  return out;
}

Mirror::Mirror(uint64_t worldSerial)
{
  m_scene = HdRetainedSceneIndex::New();
  m_root = SdfPath("/anari/w" + std::to_string(worldSerial));
}

Mirror::PrimRecord Mirror::MakeRecord(Surface *s, const GfMatrix4d &xform)
{
  PrimRecord rec;
  rec.surface = s;
  rec.xform = xform;
  Geometry *geom = s->geometry();
  if (auto *pos = geom->vertexPosition()) {
    rec.pointsPtr = pos->data();
    rec.pointsCount = pos->size();
  }
  if (auto *idx = geom->index()) {
    rec.indexPtr = idx->data();
    rec.indexCount = idx->size();
  }
  if (auto *norm = geom->vertexNormal()) {
    rec.normalsPtr = norm->data();
    rec.normalsCount = norm->size();
  }
  // Only a plain mesh (triangle/quad) authors its points zero-copy with no other
  // mutable bulk data, so an in-place points mutation can take the locator-
  // precise DirtyPrims(points) fast path. Point/curve widths and tessellated
  // cone/cylinder geometry carry copied per-vertex data, so any change there
  // forces a full rebuild (SPEC §5.2). App-provided normals are likewise a
  // decode-copy `normals` primvar (PrimBuilder), so a mesh carrying them no
  // longer qualifies for the fast path — an in-place normal mutation must
  // re-author the copy via a full rebuild. The same applies to copied UVs,
  // colors, and attributes at either vertex or face-varying interpolation.
  rec.incremental = geom->primType() == pxr::HdPrimTypeTokens->mesh
      && !geom->tessellation() && !geom->hasCopiedPrimvars();
  if (auto *mat = s->material()) {
    rec.material = mat;
    const float3 c = mat->color();
    rec.color = GfVec3f(c[0], c[1], c[2]);
    rec.opacity = mat->opacity();
    for (const auto &b : mat->samplerBindings()) {
      const Sampler *smp = b.sampler.ptr;
      if (!smp || !smp->authorsDerivedPrimvar())
        continue;
      rec.derivedSig += std::to_string(smp->serial()) + ':'
          + std::to_string(smp->revision()) + ';';
      // A derived primvar computed from the points is a copy of them, so an
      // in-place points mutation must re-author it rather than take the
      // DirtyPrims(points) fast path.
      if (smp->readsPositions())
        rec.incremental = false;
    }
  }
  return rec;
}

static bool SameOverrides(const std::optional<InstanceOverrides> &a,
    const std::optional<InstanceOverrides> &b)
{
  if (!a || !b)
    return !a && !b;
  auto same = [](const float4 &x, const float4 &y) {
    return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
  };
  if (a->hasColor != b->hasColor || !same(a->color, b->color))
    return false;
  for (int k = 0; k < 4; ++k) {
    if (a->hasAttribute[k] != b->hasAttribute[k]
        || !same(a->attribute[k], b->attribute[k]))
      return false;
  }
  return true;
}

InstanceBinding Mirror::Binding(const PrimRecord &rec)
{
  InstanceBinding b;
  b.instancerPath = rec.instancerPath;
  b.prototypeRoot = rec.prototypeRoot;
  b.constantMask = rec.instanceConstantMask;
  b.overrideMask = rec.instanceOverrideMask;
  b.values = rec.instanceValues ? &*rec.instanceValues : nullptr;
  return b;
}

bool Mirror::SamePrimTopology(const PrimRecord &a, const PrimRecord &b)
{
  return a.pointsPtr == b.pointsPtr && a.pointsCount == b.pointsCount
      && a.indexPtr == b.indexPtr && a.indexCount == b.indexCount
      && a.normalsPtr == b.normalsPtr && a.normalsCount == b.normalsCount
      && a.xform == b.xform && a.color == b.color && a.opacity == b.opacity
      && a.material == b.material && a.instancerPath == b.instancerPath
      && a.instanceConstantMask == b.instanceConstantMask
      && a.instanceOverrideMask == b.instanceOverrideMask
      && SameOverrides(a.instanceValues, b.instanceValues)
      && a.derivedSig == b.derivedSig;
}

Mirror::MatRecord Mirror::MakeMatRecord(Material *m)
{
  MatRecord r;
  r.material = m;
  const float3 c = m->color();
  const float3 e = m->emissive();
  r.color = GfVec3f(c[0], c[1], c[2]);
  r.emissive = GfVec3f(e[0], e[1], e[2]);
  r.opacity = m->opacity();
  r.metallic = m->metallic();
  r.roughness = m->roughness();
  r.ior = m->ior();
  r.clearcoat = m->clearcoat();
  r.clearcoatRoughness = m->clearcoatRoughness();
  r.occlusion = m->occlusion();
  r.alphaMode = int(m->alphaMode());
  r.opacityThreshold = m->opacityThreshold();
  // Every material consuming a changed sampler must re-author its network,
  // including consumers in other worlds and in-place image edits.
  for (const auto &b : m->samplerBindings()) {
    r.samplerSig += b.usdInput.GetString();
    r.samplerSig += ':';
    r.samplerSig += b.sampler ? std::to_string(b.sampler->serial()) : "0";
    r.samplerSig += ':';
    r.samplerSig += b.sampler ? std::to_string(b.sampler->revision()) : "0";
    r.samplerSig += ';';
  }
  // Capture attribute bindings so a bind/unbind (or a swap to a different
  // attribute) re-authors the network (SPEC §6.1).
  for (const auto &b : m->attributeBindings()) {
    r.attributeSig += b.usdInput.GetString();
    r.attributeSig += ':';
    r.attributeSig += b.primvar;
    r.attributeSig += ';';
  }
  return r;
}

bool Mirror::SameMaterial(const MatRecord &a, const MatRecord &b)
{
  return a.material == b.material && a.color == b.color
      && a.emissive == b.emissive && a.opacity == b.opacity
      && a.metallic == b.metallic && a.roughness == b.roughness
      && a.ior == b.ior && a.clearcoat == b.clearcoat
      && a.clearcoatRoughness == b.clearcoatRoughness
      && a.occlusion == b.occlusion && a.alphaMode == b.alphaMode
      && a.opacityThreshold == b.opacityThreshold
      && a.samplerSig == b.samplerSig && a.attributeSig == b.attributeSig;
}

SdfPath Mirror::materialPath(Material *m) const
{
  if (!m)
    return SdfPath();
  return m_root.AppendPath(
      SdfPath("materials/m" + std::to_string(m->serial())));
}

void Mirror::syncMaterials(const std::map<SdfPath, PrimRecord> &desired)
{
  // Distinct materials referenced by the desired rprims -> desired sprim set.
  std::map<SdfPath, MatRecord> desiredMats;
  for (const auto &kv : desired) {
    if (Material *m = kv.second.material)
      desiredMats.emplace(materialPath(m), MakeMatRecord(m));
  }

  // Remove material sprims no longer referenced.
  HdSceneIndexObserver::RemovedPrimEntries removed;
  for (const auto &kv : m_presentMaterials) {
    if (desiredMats.find(kv.first) == desiredMats.end())
      removed.emplace_back(kv.first);
  }
  if (!removed.empty())
    m_scene->RemovePrims(removed);

  // Add new / changed material sprims (a retained AddPrims replaces in place).
  for (const auto &kv : desiredMats) {
    auto it = m_presentMaterials.find(kv.first);
    if (it == m_presentMaterials.end() || !SameMaterial(it->second, kv.second)) {
      m_scene->AddPrims({{kv.first, HdPrimTypeTokens->material,
          BuildMaterialPrim(kv.second.material)}});
    }
  }

  m_presentMaterials = std::move(desiredMats);
}

bool Mirror::SameInstancer(const InstancerRecord &a, const InstancerRecord &b)
{
  return a.prototypes == b.prototypes && a.transforms == b.transforms
      && a.attributes == b.attributes;
}

void Mirror::syncInstancers(
    const std::map<SdfPath, InstancerRecord> &desiredInstancers)
{
  HdSceneIndexObserver::RemovedPrimEntries removed;
  for (const auto &kv : m_presentInstancers) {
    if (desiredInstancers.find(kv.first) == desiredInstancers.end())
      removed.emplace_back(kv.first);
  }
  if (!removed.empty())
    m_scene->RemovePrims(removed);

  for (const auto &kv : desiredInstancers) {
    auto it = m_presentInstancers.find(kv.first);
    if (it == m_presentInstancers.end()
        || !SameInstancer(it->second, kv.second)) {
      m_scene->AddPrims({{kv.first, HdPrimTypeTokens->instancer,
          BuildInstancerPrim(kv.second.prototypes, kv.second.transforms,
              kv.second.attributes)}});
    }
  }

  m_presentInstancers = desiredInstancers;
}

SdfPath Mirror::lightPath(Light *l) const
{
  return m_root.AppendPath(SdfPath("lights/l" + std::to_string(l->serial())));
}

SdfPath Mirror::expandedLightPath(
    uint64_t instanceSerial, int element, uint64_t lightSerial) const
{
  std::string name = "lights/i" + std::to_string(instanceSerial);
  if (element >= 0)
    name += "_t" + std::to_string(element);
  return m_root.AppendPath(SdfPath(name + "_l" + std::to_string(lightSerial)));
}

void Mirror::syncLights(World *world)
{
  std::vector<SdfPath> desiredPaths;

  // Re-author every present light (params are cheap and change detection on the
  // free-form param list is not worth a record; sync only runs when something
  // was committed, SPEC §5.2). Re-authoring the full desired set each sync also
  // satisfies §6.8's "light-param changes fan out to all expanded sprims" for
  // free — no separate dependent-prims map is needed.

  // World-direct lights: /anari/w{N}/lights/l{L} (SPEC §6.6).
  for (Light *l : world->gatherLights()) {
    const SdfPath path = lightPath(l);
    desiredPaths.push_back(path);
    LightData data = l->buildLightData();
    m_scene->AddPrims({{path, data.type, BuildLightPrim(data)}});
  }

  // Instanced-group light expansion (SPEC §6.8): each group light expands to one
  // sprim per (instance, light) pair at /anari/w{N}/lights/i{I}_l{L} (per
  // (instance, element, light) for array transforms), N×M for N flattened
  // transforms × M lights (linear, uncapped). The sprim's transform is the light's
  // local transform composed with the instance transform; in the GfMatrix
  // row-vector convention ToGfMatrix documents (p' = p*M), local-then-instance
  // is p * Mlocal * Minstance.
  for (const auto &ig : world->gatherInstancedGroups()) {
    const std::vector<Light *> groupLights = ig.group->lights();
    if (groupLights.empty())
      continue;
    for (size_t i = 0; i < ig.transforms.size(); ++i) {
      const uint64_t instSerial = ig.instanceSerials[i];
      const GfMatrix4d instXform = ToGfMatrix(ig.transforms[i]);
      for (Light *l : groupLights) {
        LightData data = l->buildLightData();
        data.xform = data.xform * instXform;
        const SdfPath path =
            expandedLightPath(instSerial, ig.elements[i], l->serial());
        desiredPaths.push_back(path);
        m_scene->AddPrims({{path, data.type, BuildLightPrim(data)}});
      }
    }
  }

  // Remove lights no longer present.
  HdSceneIndexObserver::RemovedPrimEntries removed;
  for (const SdfPath &p : m_presentLights) {
    if (std::find(desiredPaths.begin(), desiredPaths.end(), p)
        == desiredPaths.end())
      removed.emplace_back(p);
  }
  if (!removed.empty())
    m_scene->RemovePrims(removed);

  m_presentLights = std::move(desiredPaths);
}

SdfPath Mirror::fieldPath(const SpatialField *f) const
{
  return m_root.AppendPath(SdfPath("fields/f" + std::to_string(f->serial())));
}

SdfPath Mirror::volumeMaterialPath() const
{
  return m_root.AppendPath(SdfPath("volumeMaterial"));
}

SdfPath Mirror::volumeTablePath(const Volume *v) const
{
  return m_root.AppendPath(
      SdfPath("volumeTables/v" + std::to_string(v->serial())));
}

// Register a classification table under a new asset path. Asset paths are
// process-global (AnariFieldRegistry) while mirrors are per world, so table
// revisions come from one process-wide counter.
static FieldSnapshotPtr PublishTable(AnariFieldDataPtr table)
{
  static std::atomic<uint64_t> counter{0};
  const uint64_t revision = ++counter;
  return std::make_shared<const FieldSnapshot>(revision,
      "anari://field/table_r" + std::to_string(revision) + ".anari",
      std::move(table));
}

void Mirror::syncVolumes(
    const std::map<SdfPath, std::pair<Volume *, VolumeRecord>> &desired)
{
  // Field bprims (one per field, shared by its volumes) and classification
  // table bprims (one per volume) the desired rprims reference.
  std::map<SdfPath, FieldSnapshotPtr> fields;
  std::map<SdfPath, std::pair<VolumeClassificationPtr, FieldSnapshotPtr>>
      tables;
  for (const auto &kv : desired) {
    const VolumeRecord &r = kv.second.second;
    fields.emplace(fieldPath(r.volume->field.ptr), r.field);
    tables.emplace(
        volumeTablePath(kv.second.first), std::make_pair(r.volume, r.field));
  }

  HdSceneIndexObserver::RemovedPrimEntries removed;
  for (const auto &kv : m_presentVolumes)
    if (!desired.count(kv.first))
      removed.emplace_back(kv.first);
  for (const auto &kv : m_presentFields)
    if (!fields.count(kv.first))
      removed.emplace_back(kv.first);
  for (const auto &kv : m_presentVolumeTables)
    if (!tables.count(kv.first))
      removed.emplace_back(kv.first);
  if (m_hasVolumeMaterial && desired.empty()) {
    removed.emplace_back(volumeMaterialPath());
    m_hasVolumeMaterial = false;
  }
  if (!removed.empty())
    m_scene->RemovePrims(removed);

  // A new field snapshot is a new asset path (a new texture identity).
  for (const auto &kv : fields) {
    auto it = m_presentFields.find(kv.first);
    if (it == m_presentFields.end() || it->second != kv.second)
      m_scene->AddPrims(
          {{kv.first, FieldPrimType(), BuildFieldPrim(*kv.second)}});
  }
  m_presentFields = std::move(fields);

  // A classification or field edit publishes a new table under a new asset
  // path. The table holds the field's domain, so a field edit rebuilds it too.
  std::map<SdfPath, VolumeTableRecord> presentTables;
  for (const auto &kv : tables) {
    auto it = m_presentVolumeTables.find(kv.first);
    if (it != m_presentVolumeTables.end()
        && it->second.volume == kv.second.first
        && it->second.field == kv.second.second) {
      presentTables.emplace(kv.first, it->second);
      continue;
    }
    VolumeTableRecord rec{kv.second.first,
        kv.second.second,
        PublishTable(
            BuildClassificationTable(*kv.second.first, *kv.second.second))};
    m_scene->AddPrims(
        {{kv.first, FieldPrimType(), BuildFieldPrim(*rec.table)}});
    presentTables.emplace(kv.first, std::move(rec));
  }
  m_presentVolumeTables = std::move(presentTables);

  // One material serves every volume: it never changes after this.
  if (!m_hasVolumeMaterial && !desired.empty()) {
    m_scene->AddPrims({{volumeMaterialPath(), HdPrimTypeTokens->material,
        BuildVolumeMaterialPrim()}});
    m_hasVolumeMaterial = true;
  }

  // Re-adding the rprim resyncs it whole (Storm re-allocates its field
  // textures), which a changed field or table needs: Storm does not refresh
  // them from the bprim change alone.
  std::map<SdfPath, VolumeRecord> present;
  for (const auto &kv : desired) {
    const VolumeRecord &r = kv.second.second;
    present.emplace(kv.first, r);
    auto it = m_presentVolumes.find(kv.first);
    if (it != m_presentVolumes.end() && it->second == r)
      continue;
    m_scene->AddPrims({{kv.first, HdPrimTypeTokens->volume,
        BuildVolumePrim(*r.volume, *r.field, fieldPath(r.volume->field.ptr),
            volumeTablePath(kv.second.first), volumeMaterialPath(),
            r.instancerPath, r.prototypeRoot)}});
  }
  m_presentVolumes = std::move(present);
}

void Mirror::sync(World *world)
{
  std::vector<World::Renderable> renderables = world->gatherRenderables();
  std::vector<World::InstancedGroup> groups = world->gatherInstancedGroups();

  // Desired prim set, keyed by the serial-ID taxonomy path (SPEC §4.2).
  std::map<SdfPath, PrimRecord> desired;
  std::map<SdfPath, InstancerRecord> desiredInstancers;
  std::map<SdfPath, std::pair<Volume *, VolumeRecord>> desiredVolumes;
  // Snapshot a volume and its field once; false when either is not
  // renderable (no commit yet, no field, or an invalid field).
  auto volumeRecord = [](Volume *v, VolumeRecord &r) {
    r.volume = v->classification();
    r.field = r.volume && r.volume->field ? r.volume->field->snapshot()
                                          : nullptr;
    return bool(r.field);
  };

  m_pickingIds.clear();

  // World-direct volumes: /anari/w{N}/volumes/v{V}, identity transform.
  // Volumes take no picking IDs: Storm's volume pass does not produce usable
  // ID output, so ID channels read "no hit" through them (README.md).
  for (Volume *v : world->gatherVolumes()) {
    VolumeRecord r;
    if (!volumeRecord(v, r))
      continue;
    desiredVolumes.emplace(m_root.AppendPath(SdfPath(
                               "volumes/v" + std::to_string(v->serial()))),
        std::make_pair(v, r));
  }

  // World-direct surfaces: /anari/w{N}/surfaces/s{S}, identity transform.
  for (const auto &r : renderables) {
    SdfPath path = m_root.AppendPath(
        SdfPath("surfaces/s" + std::to_string(r.surfaceSerial)));
    desired.emplace(path, MakeRecord(r.surface, GfMatrix4d(1)));
    m_pickingIds[path] = {r.surface->id(),
        r.surface->geometry()->pickingIds(),
        {},
        r.surface->geometry()->pickingIds(true)};
  }

  // Instanced groups (SPEC §4.3/§4.2): one instancer per (world, group) at
  // /anari/w{N}/instancers/g{G}, with the group's surfaces as its prototype
  // rprims /anari/w{N}/instancers/g{G}/s{S} — authored once with an identity
  // xform (the instancer supplies each instance's transform). Every instance
  // draws every prototype.
  for (const auto &ig : groups) {
    const uint64_t g = ig.group->serial();
    const SdfPath instPath =
        m_root.AppendPath(SdfPath("instancers/g" + std::to_string(g)));

    // Slots some instance of the group overrides (SPEC §6.8).
    uint32_t overrideMask = 0;
    for (const auto &o : ig.overrides) {
      overrideMask |= o.hasColor ? 1u << 4 : 0u;
      for (int k = 0; k < 4; ++k)
        overrideMask |= o.hasAttribute[k] ? 1u << k : 0u;
    }

    InstancerRecord ir;
    std::vector<InstancerAttribute> derivedAttributes;
    for (Surface *s : ig.group->surfaces()) {
      if (!s->isValid())
        continue;
      // A world-space sampler source differs per instance at every vertex,
      // which a shared prototype cannot carry: author one rprim per instance
      // at /anari/w{N}/surfaces/i{I}[_t{k}]_s{S}, with the instance transform
      // and the instance's overrides as constants (SPEC §6.4). Linear and
      // uncapped in the instance count, like the light expansion (§6.8).
      if (ReadsWorldSpace(s->material())) {
        for (size_t i = 0; i < ig.transforms.size(); ++i) {
          std::string name = "surfaces/i" + std::to_string(ig.instanceSerials[i]);
          if (ig.elements[i] >= 0)
            name += "_t" + std::to_string(ig.elements[i]);
          const SdfPath copy = m_root.AppendPath(
              SdfPath(name + "_s" + std::to_string(s->serial())));
          PrimRecord rec = MakeRecord(s, ToGfMatrix(ig.transforms[i]));
          rec.instanceValues = ig.overrides[i];
          desired.emplace(copy, rec);
          m_pickingIds[copy] = {s->id(),
              s->geometry()->pickingIds(),
              {ig.ids[i]},
              s->geometry()->pickingIds(true),
              true};
        }
        continue;
      }
      SdfPath path =
          instPath.AppendPath(SdfPath("s" + std::to_string(s->serial())));
      PrimRecord rec = MakeRecord(s, GfMatrix4d(1));
      rec.instanceOverrideMask = overrideMask;
      // Hydra prototype constants shadow inherited instance primvars. When
      // both are present, resolve constant fallbacks per prototype instancer.
      for (int k = 0; k < 5; ++k) {
        if (!s->geometry()->constantAttribute(k))
          continue;
        const bool anyOverride =
            std::any_of(ig.overrides.begin(), ig.overrides.end(),
                [k](const InstanceOverrides &o) {
                  return k == 4 ? o.hasColor : o.hasAttribute[k];
                });
        if (anyOverride)
          rec.instanceConstantMask |= 1u << k;
      }
      rec.instancerPath = rec.instanceConstantMask
          ? m_root.AppendPath(SdfPath("instancers/g" + std::to_string(g)
                + "_s" + std::to_string(s->serial())))
          : instPath;
      if (rec.instanceConstantMask) {
        auto overrides = ig.overrides;
        for (int k = 0; k < 5; ++k) {
          if (!(rec.instanceConstantMask & (1u << k)))
            continue;
          const auto &constant = *s->geometry()->constantAttribute(k);
          for (auto &o : overrides) {
            bool &present = k == 4 ? o.hasColor : o.hasAttribute[k];
            if (!present) {
              (k == 4 ? o.color : o.attribute[k]) = constant;
              present = true;
            }
          }
        }
        InstancerRecord perSurface;
        perSurface.prototypes.push_back(path);
        for (const auto &t : ig.transforms)
          perSurface.transforms.push_back(ToGfMatrix(t));
        perSurface.attributes = BuildInstanceAttributes(overrides);
        for (auto &a : BuildDerivedInstanceAttributes(s->material(), overrides))
          perSurface.attributes.push_back(std::move(a));
        desiredInstancers.emplace(rec.instancerPath, std::move(perSurface));
      }
      rec.prototypeRoot = path;   // each prototype rprim is its own root
      desired.emplace(path, rec);
      m_pickingIds[path] = {s->id(),
          s->geometry()->pickingIds(),
          ig.ids,
          s->geometry()->pickingIds(true)};
      if (!rec.instanceConstantMask) {
        ir.prototypes.push_back(path);
        // Named by sampler serial, so surfaces sharing a sampler share the
        // values: no fill differs without a constant (which splits above).
        for (auto &a : BuildDerivedInstanceAttributes(s->material(), ig.overrides)) {
          if (std::none_of(derivedAttributes.begin(), derivedAttributes.end(),
                  [&](const InstancerAttribute &o) { return o.first == a.first; }))
            derivedAttributes.push_back(std::move(a));
        }
      }
    }

    // The group's volumes are prototypes of the same instancer (SPEC §7).
    for (Volume *v : ig.group->volumes()) {
      VolumeRecord r;
      if (!volumeRecord(v, r))
        continue;
      const SdfPath path =
          instPath.AppendPath(SdfPath("v" + std::to_string(v->serial())));
      r.instancerPath = instPath;
      r.prototypeRoot = path;
      desiredVolumes.emplace(path, std::make_pair(v, r));
      ir.prototypes.push_back(path);
    }

    for (const auto &t : ig.transforms)
      ir.transforms.push_back(ToGfMatrix(t));
    ir.attributes = BuildInstanceAttributes(ig.overrides);
    for (auto &a : derivedAttributes)
      ir.attributes.push_back(std::move(a));
    if (!ir.prototypes.empty())
      desiredInstancers.emplace(instPath, ir);
  }

  // Remove rprims no longer reachable.
  HdSceneIndexObserver::RemovedPrimEntries removed;
  for (const auto &kv : m_present) {
    if (desired.find(kv.first) == desired.end())
      removed.emplace_back(kv.first);
  }
  if (!removed.empty())
    m_scene->RemovePrims(removed);

  // Author material sprims + instancers before the rprims that reference them.
  syncMaterials(desired);
  syncInstancers(desiredInstancers);
  syncLights(world);
  syncVolumes(desiredVolumes);

  // Add new prims; dirty changed ones incrementally.
  HdSceneIndexObserver::DirtiedPrimEntries dirtied;

  for (const auto &kv : desired) {
    const SdfPath &path = kv.first;
    const PrimRecord &rec = kv.second;
    const TfToken primType = rec.surface->geometry()->primType();
    const SdfPath matPath = materialPath(rec.material);
    auto it = m_present.find(path);
    if (it == m_present.end()) {
      // New prim: full build.
      m_scene->AddPrims({{path, primType,
          BuildSurfacePrim(
              rec.surface, rec.xform, matPath, Binding(rec))}});
    } else if (rec.incremental && it->second.incremental
        && SamePrimTopology(it->second, rec)) {
      // In-place bulk mutation: the retained data source still wraps the live
      // external buffer, so a locator-precise DirtyPrims(points) re-pulls the
      // mutated memory with no topology resync (proto 012).
      dirtied.emplace_back(
          path, HdDataSourceLocatorSet{PointsLocator()});
    } else {
      // Pointer / topology / transform / material changed, or a rebuild-only
      // geometry type (points/curve/tessellated): full rebuild.
      m_scene->AddPrims({{path, primType,
          BuildSurfacePrim(
              rec.surface, rec.xform, matPath, Binding(rec))}});
    }
  }

  if (!dirtied.empty())
    m_scene->DirtyPrims(dirtied);

  m_present = std::move(desired);
}

} // namespace anari_hydra
