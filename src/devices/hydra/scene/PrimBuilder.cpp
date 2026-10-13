// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PrimBuilder.h"
#include "ZeroCopyDataSource.h"
#include "geometry/Geometry.h"
#include "material/Material.h"
#include "sampler/Sampler.h"

#include <pxr/imaging/hd/basisCurvesSchema.h>
#include <pxr/imaging/hd/basisCurvesTopologySchema.h>
#include <pxr/imaging/hd/extentSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/meshSchema.h>
#include <pxr/imaging/hd/meshTopologySchema.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include <pxr/imaging/hd/primvarSchema.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/purposeSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/path.h>

#include <anari/frontend/type_utility.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

HdDataSourceLocator PointsLocator()
{
  return HdDataSourceLocator(
      HdPrimvarsSchemaTokens->primvars, HdTokens->points);
}

std::string AttributePrimvarName(int index)
{
  return "attribute" + std::to_string(index);
}

// --- shared primvar / trailing-schema helpers ------------------------------

static HdContainerDataSourceHandle PointsPrimvar(
    const HdSampledDataSourceHandle &pointsDS)
{
  return HdPrimvarSchema::BuildRetained(pointsDS,
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->vertex),
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->point),
      nullptr,
      nullptr);
}

static HdContainerDataSourceHandle WidthsPrimvar(
    const HdSampledDataSourceHandle &widthsDS, bool perVertex)
{
  return HdPrimvarSchema::BuildRetained(widthsDS,
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(perVertex
              ? HdPrimvarSchemaTokens->vertex
              : HdPrimvarSchemaTokens->constant),
      nullptr,
      nullptr,
      nullptr);
}

static HdContainerDataSourceHandle ColorPrimvar(const GfVec3f &c)
{
  return HdPrimvarSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfVec3f>::New(c),
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->constant),
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->color),
      nullptr,
      nullptr);
}

static HdContainerDataSourceHandle OpacityPrimvar(float op)
{
  return HdPrimvarSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<float>::New(op),
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdPrimvarSchemaTokens->constant),
      nullptr,
      nullptr,
      nullptr);
}

// Preserve the array's interpolation and semantic role at the Hydra boundary.
static HdContainerDataSourceHandle ArrayPrimvar(
    const HdSampledDataSourceHandle &values,
    const TfToken &interpolation,
    const TfToken &role = TfToken())
{
  return HdPrimvarSchema::BuildRetained(values,
      nullptr,
      nullptr,
      HdRetainedTypedSampledDataSource<TfToken>::New(interpolation),
      role.IsEmpty() ? nullptr
                     : HdRetainedTypedSampledDataSource<TfToken>::New(role),
      nullptr,
      nullptr);
}

// An optional per-element map applied to the padded float4 attribute value
// before it is narrowed to the primvar width (used for derived primvars).
using AttrTransform = std::function<helium::float4(const helium::float4 &)>;

// A per-element float4 source, indexed by Hydra element.
using ElementSource = std::function<helium::float4(size_t)>;

// Narrow `n` float4 elements to a primvar of `width` components (1..4).
static HdSampledDataSourceHandle BuildFloatArray(
    size_t n, int width, const ElementSource &read)
{
  switch (width) {
  case 1: {
    VtFloatArray v(n);
    for (size_t i = 0; i < n; ++i)
      v[i] = read(i).x;
    return HdRetainedTypedSampledDataSource<VtFloatArray>::New(v);
  }
  case 2: {
    VtVec2fArray v(n);
    for (size_t i = 0; i < n; ++i) {
      const auto x = read(i);
      v[i] = GfVec2f(x.x, x.y);
    }
    return HdRetainedTypedSampledDataSource<VtVec2fArray>::New(v);
  }
  case 4: {
    VtVec4fArray v(n);
    for (size_t i = 0; i < n; ++i) {
      const auto x = read(i);
      v[i] = GfVec4f(x.x, x.y, x.z, x.w);
    }
    return HdRetainedTypedSampledDataSource<VtVec4fArray>::New(v);
  }
  default: { // 3
    VtVec3fArray v(n);
    for (size_t i = 0; i < n; ++i) {
      const auto x = read(i);
      v[i] = GfVec3f(x.x, x.y, x.z);
    }
    return HdRetainedTypedSampledDataSource<VtVec3fArray>::New(v);
  }
  }
}

// A single float4 as a constant-interpolation primvar of `width` components.
static HdSampledDataSourceHandle ConstantFloatValue(
    const helium::float4 &v, int width)
{
  if (width == 1)
    return HdRetainedTypedSampledDataSource<float>::New(v.x);
  if (width == 2)
    return HdRetainedTypedSampledDataSource<GfVec2f>::New(GfVec2f(v.x, v.y));
  if (width == 3)
    return HdRetainedTypedSampledDataSource<GfVec3f>::New(
        GfVec3f(v.x, v.y, v.z));
  return HdRetainedTypedSampledDataSource<GfVec4f>::New(
      GfVec4f(v.x, v.y, v.z, v.w));
}

// Decode an ANARI attribute array to a primvar of `width` components
// (1..4), via readAsAttributeValue (which linearises and widens any element
// type to float4). Copied — small and outside the zero-copy scope.
static HdSampledDataSourceHandle DecodeAttributeArray(helium::Array1D *a,
    size_t n,
    int width,
    const std::vector<uint32_t> *indices = nullptr,
    const AttrTransform &transform = {})
{
  return BuildFloatArray(n, width, [&](size_t i) {
    const helium::float4 v =
        a->readAsAttributeValue(int(indices ? (*indices)[i] : i));
    return transform ? transform(v) : v;
  });
}

// A derived primvar baked per rprim for one sampler (SPEC §6.4): `name`
// receives `transform` applied to the padded float4 value of element source
// `slot` (Sampler's slot table: attributeN / color, or a geometry-derived
// source), narrowed to `width` components at the source's own interpolation.
// Image samplers on the baked path derive their st coordinates this way and
// transform samplers their value; a primitive sampler sets `primitive` instead
// and indexes its array by ANARI primitive (uniform interpolation).
struct DerivedPrimvar
{
  TfToken name;
  int slot{0};
  int width{2};
  AttrTransform transform;
  const Sampler *primitive{nullptr};
  // A source this geometry lacks authors `transform` of ANARI's missing-
  // attribute value (0,0,0,1) as a constant (transform samplers); otherwise
  // nothing is authored and the reader's fallback applies (image samplers).
  bool constantWhenAbsent{false};
};

// The rprim's authored geometry, as the element sources for the geometry-
// derived sampler inputs (SPEC §6.4): the points actually authored (the ANARI
// vertices, or the tessellated surface), the tessellator's analytic normals
// where it supplies them, and the surface transform for the world-space
// variants (row-vector convention, as authored on the xform schema).
struct GeometryFrame
{
  const GfVec3f *points{nullptr};
  size_t numPoints{0};
  const GfVec3f *generatedNormals{nullptr};
  size_t numGeneratedNormals{0};
  GfMatrix4d xform{1};
};

// The constant value of attribute slot `slot` (Sampler's numbering): a
// per-instance copy's override, else the geometry constant unless the
// instancer carries it (SPEC §4.1/§6.8).
static const helium::float4 *ConstantFor(
    Geometry *geom, const InstanceBinding &instancing, int slot)
{
  if (!Sampler::IsAttributeSlot(slot))
    return nullptr;
  if (const InstanceOverrides *o = instancing.values) {
    if (slot == Sampler::kSlotColor ? o->hasColor : o->hasAttribute[slot])
      return slot == Sampler::kSlotColor ? &o->color : &o->attribute[slot];
  }
  if (instancing.constantMask & (1u << slot))
    return nullptr;
  return geom->constantAttribute(slot);
}

// Geometry precedence is corner > vertex > primitive > constant. Generated
// meshes expand vertex and primitive data through their original source maps.
// Only native triangle/quad meshes supply numCorners for face-varying data.
static std::vector<std::pair<TfToken, HdDataSourceBaseHandle>>
GatherOptionalPrimvars(Geometry *geom,
    size_t numVerts,
    size_t numCorners = 0,
    const std::vector<uint32_t> *indices = nullptr,
    const std::vector<uint32_t> *primitives = nullptr,
    const InstanceBinding &instancing = {},
    const std::vector<DerivedPrimvar> &derived = {},
    const GeometryFrame &frame = {})
{
  std::vector<std::pair<TfToken, HdDataSourceBaseHandle>> out;
  auto append = [&](const TfToken &name,
                    helium::Array1D *vertex,
                    helium::Array1D *corners,
                    int slot,
                    int width,
                    const TfToken &role = TfToken(),
                    const AttrTransform &transform = {}) {
    const bool faceVarying = numCorners > 0 && corners;
    auto *a = faceVarying ? corners : vertex;
    const bool uniform = !a && slot >= 0 && geom->primitiveAttribute(slot);
    if (uniform)
      a = geom->primitiveAttribute(slot);
    const auto *mapping = uniform ? primitives : indices;
    if (!a || a->size() == 0) {
      const auto *cv = ConstantFor(geom, instancing, slot);
      if (cv) {
        const helium::float4 mapped = transform ? transform(*cv) : *cv;
        out.emplace_back(name,
            ArrayPrimvar(ConstantFloatValue(mapped, width),
                HdPrimvarSchemaTokens->constant,
                role));
      }
      return;
    }
    if (mapping
        && std::any_of(mapping->begin(), mapping->end(), [&](uint32_t i) {
             return i >= a->size();
           }))
      return;
    const size_t n = mapping
        ? mapping->size()
        : std::min(faceVarying ? numCorners : numVerts, a->size());
    const int components =
        width ? width : int(anari::componentsOf(a->elementType()));
    out.emplace_back(name,
        ArrayPrimvar(DecodeAttributeArray(a, n, components, mapping, transform),
            uniform           ? HdPrimvarSchemaTokens->uniform
                : faceVarying ? HdPrimvarSchemaTokens->faceVarying
                              : HdPrimvarSchemaTokens->vertex,
            role));
  };
  append(TfToken("st"),
      geom->vertexAttribute0(),
      geom->faceVaryingAttribute0(),
      0,
      2);
  append(TfToken("color"), geom->vertexColor(), geom->faceVaryingColor(), 4, 3);
  helium::Array1D *vertex[4] = {geom->vertexAttribute0(),
      geom->vertexAttribute1(),
      geom->vertexAttribute2(),
      geom->vertexAttribute3()};
  helium::Array1D *corners[4] = {geom->faceVaryingAttribute0(),
      geom->faceVaryingAttribute1(),
      geom->faceVaryingAttribute2(),
      geom->faceVaryingAttribute3()};
  for (int k = 0; k < 4; ++k)
    append(TfToken(AttributePrimvarName(k)), vertex[k], corners[k], k, 0);
  // Typed aliases for material readers: float / float3 for every attribute,
  // float2 (sampler coordinates, SPEC §6.4) for all but attribute0, whose
  // float2 form is the `st` primvar above.
  for (int k = 0; k < 5; ++k) {
    const std::string name = k == 4 ? "color" : AttributePrimvarName(k);
    for (int width : {1, 2, 3}) {
      if (width == 2 && k == 0)
        continue;
      append(TfToken(name + "_float" + std::to_string(width)),
          k == 4 ? geom->vertexColor() : vertex[k],
          k == 4 ? geom->faceVaryingColor() : corners[k],
          k,
          width);
    }
  }
  // Derived sampler primvars (SPEC §6.4). An attribute source goes through
  // `append` (corner > vertex > primitive > constant precedence, instance
  // masks) with the sampler's map applied per element; a geometry-derived
  // source reads the authored geometry itself. Either way the result is exact:
  // interpolation commutes with the affine maps involved.
  for (const auto &d : derived) {
    const size_t before = out.size();
    size_t count = 0;
    TfToken interpolation;
    ElementSource source;
    if (d.primitive) {
      // array[primitiveId + inOffset] per Hydra face/segment, by ANARI
      // primitive index (generated geometry expands one ANARI primitive to
      // many faces; `primitives` maps them back).
      const Sampler *sampler = d.primitive;
      count = primitives ? primitives->size() : 0;
      interpolation = HdPrimvarSchemaTokens->uniform;
      source = [sampler, primitives](size_t i) {
        return sampler->primitiveValue((*primitives)[i]);
      };
    } else if (Sampler::IsAttributeSlot(d.slot)) {
      append(d.name,
          d.slot == Sampler::kSlotColor ? geom->vertexColor() : vertex[d.slot],
          d.slot == Sampler::kSlotColor ? geom->faceVaryingColor()
                                        : corners[d.slot],
          d.slot,
          d.width,
          TfToken(),
          d.transform);
    } else if (d.slot == Sampler::kSlotObjectPosition
        || d.slot == Sampler::kSlotWorldPosition) {
      // The authored points: ANARI vertices, or the tessellated surface, so a
      // sphere reads its surface position rather than its centre.
      const bool world = d.slot == Sampler::kSlotWorldPosition;
      count = frame.numPoints;
      interpolation = HdPrimvarSchemaTokens->vertex;
      source = [&frame, world](size_t i) {
        GfVec3f p = frame.points[i];
        if (world)
          p = GfVec3f(frame.xform.Transform(p));
        return helium::float4(p[0], p[1], p[2], 1.f);
      };
    } else if (d.slot == Sampler::kSlotObjectNormal
        || d.slot == Sampler::kSlotWorldNormal) {
      // App-provided normals only (corner, then vertex, as the `normals`
      // primvar below), or the tessellator's analytic normals for generated
      // surfaces; a geometry with neither has no normal to read.
      const bool world = d.slot == Sampler::kSlotWorldNormal;
      // Row-vector convention: n' = n · (M⁻¹)ᵀ, then renormalise.
      const GfMatrix4d normalXform = frame.xform.GetInverse().GetTranspose();
      auto finish = [world, normalXform](GfVec3f n) {
        if (world)
          n = GfVec3f(normalXform.TransformDir(n)).GetNormalized();
        return helium::float4(n[0], n[1], n[2], 1.f);
      };
      helium::Array1D *fv = geom->faceVaryingNormal();
      helium::Array1D *vn = geom->vertexNormal();
      if (numCorners > 0 && fv && fv->size() > 0) {
        count = std::min(numCorners, fv->size());
        interpolation = HdPrimvarSchemaTokens->faceVarying;
        source = [fv, finish](size_t i) {
          const helium::float4 n = fv->readAsAttributeValue(int(i));
          return finish(GfVec3f(n.x, n.y, n.z));
        };
      } else if (numCorners > 0 && vn && vn->size() > 0) {
        count = std::min(numVerts, vn->size());
        interpolation = HdPrimvarSchemaTokens->vertex;
        source = [vn, finish](size_t i) {
          const helium::float4 n = vn->readAsAttributeValue(int(i));
          return finish(GfVec3f(n.x, n.y, n.z));
        };
      } else if (frame.generatedNormals && frame.numGeneratedNormals > 0) {
        count = frame.numGeneratedNormals;
        interpolation = HdPrimvarSchemaTokens->vertex;
        source = [&frame, finish](
                     size_t i) { return finish(frame.generatedNormals[i]); };
      }
    } else if (d.slot == Sampler::kSlotPrimitiveId) {
      // The ANARI primitive index (not `primitive.id`, which is a picking
      // channel value), padded to (id, 0, 0, 1); uniform.
      count = primitives ? primitives->size() : 0;
      interpolation = HdPrimvarSchemaTokens->uniform;
      source = [primitives](size_t i) {
        return helium::float4(float((*primitives)[i]), 0.f, 0.f, 1.f);
      };
    }
    if (source && count > 0) {
      const AttrTransform &transform = d.transform;
      out.emplace_back(d.name,
          ArrayPrimvar(BuildFloatArray(count,
                           d.width,
                           [&](size_t i) {
                             const helium::float4 v = source(i);
                             return transform ? transform(v) : v;
                           }),
              interpolation));
    } else if (out.size() == before && d.constantWhenAbsent) {
      const helium::float4 missing(0.f, 0.f, 0.f, 1.f);
      out.emplace_back(d.name,
          ArrayPrimvar(
              ConstantFloatValue(
                  d.transform ? d.transform(missing) : missing, d.width),
              HdPrimvarSchemaTokens->constant));
    }
  }
  if (numCorners > 0)
    append(HdTokens->normals,
        geom->vertexNormal(),
        geom->faceVaryingNormal(),
        -1,
        3,
        HdPrimvarSchemaTokens->normal);
  return out;
}

// Assemble a primvars container from (name,value) pairs, appending the optional
// per-vertex material primvars.
static HdContainerDataSourceHandle PrimvarsContainer(std::vector<TfToken> names,
    std::vector<HdDataSourceBaseHandle> values,
    const std::vector<std::pair<TfToken, HdDataSourceBaseHandle>> &extras)
{
  for (const auto &e : extras) {
    names.push_back(e.first);
    values.push_back(e.second);
  }
  return HdRetainedContainerDataSource::New(
      names.size(), names.data(), values.data());
}

// Purpose / visibility / xform / extent / material binding — identical for
// every rprim type.
static void AppendTrailingSchemas(std::vector<TfToken> &names,
    std::vector<HdDataSourceBaseHandle> &values,
    const GfMatrix4d &xform,
    const GfVec3d &lo,
    const GfVec3d &hi,
    const SdfPath &materialPath)
{
  if (!materialPath.IsEmpty()) {
    // Bind the rprim to its material sprim under the all-purpose token
    // (SPEC §6.1). Delegates that ignore networks fall back to displayColor.
    const TfToken allPurpose = HdMaterialBindingsSchemaTokens->allPurpose;
    HdDataSourceBaseHandle binding = HdMaterialBindingSchema::BuildRetained(
        HdRetainedTypedSampledDataSource<SdfPath>::New(materialPath));
    names.push_back(HdMaterialBindingsSchema::GetSchemaToken());
    values.push_back(
        HdMaterialBindingsSchema::BuildRetained(1, &allPurpose, &binding));
  }

  names.push_back(HdPurposeSchemaTokens->purpose);
  values.push_back(HdPurposeSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<TfToken>::New(
          HdRenderTagTokens->geometry),
      nullptr,
      nullptr));

  names.push_back(HdVisibilitySchemaTokens->visibility);
  values.push_back(HdVisibilitySchema::BuildRetained(
      HdRetainedTypedSampledDataSource<bool>::New(true)));

  names.push_back(HdXformSchemaTokens->xform);
  values.push_back(HdXformSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfMatrix4d>::New(xform),
      HdRetainedTypedSampledDataSource<bool>::New(false)));

  names.push_back(HdExtentSchemaTokens->extent);
  values.push_back(HdExtentSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfVec3d>::New(lo),
      HdRetainedTypedSampledDataSource<GfVec3d>::New(hi)));
}

static void ComputeExtent(
    const GfVec3f *points, size_t n, GfVec3d &lo, GfVec3d &hi)
{
  lo = GfVec3d(std::numeric_limits<double>::max());
  hi = GfVec3d(std::numeric_limits<double>::lowest());
  for (size_t i = 0; i < n; ++i)
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], double(points[i][k]));
      hi[k] = std::max(hi[k], double(points[i][k]));
    }
  if (n == 0) {
    lo = GfVec3d(-1);
    hi = GfVec3d(1);
  }
}

// --- mesh (triangle / quad / tessellated cone-cylinder) --------------------

// Flatten a face-vertex-count/index topology into a mesh container. Points is a
// prepared data source (zero-copy for indexed geometry, retained-copy for
// tessellated geometry).
static HdContainerDataSourceHandle BuildMeshPrim(
    const HdSampledDataSourceHandle &pointsDS,
    const VtIntArray &faceVertexCounts,
    const VtIntArray &faceVertexIndices,
    const GfVec3d &lo,
    const GfVec3d &hi,
    const GfVec3f &color,
    float opacity,
    const GfMatrix4d &xform,
    const SdfPath &materialPath,
    const std::vector<std::pair<TfToken, HdDataSourceBaseHandle>> &extras)
{
  std::vector<TfToken> names{
      HdMeshSchemaTokens->mesh, HdPrimvarsSchemaTokens->primvars};
  std::vector<HdDataSourceBaseHandle> values;
  values.push_back(HdMeshSchema::BuildRetained(
      HdMeshTopologySchema::BuildRetained(
          HdRetainedTypedSampledDataSource<VtIntArray>::New(faceVertexCounts),
          HdRetainedTypedSampledDataSource<VtIntArray>::New(faceVertexIndices),
          nullptr,
          HdRetainedTypedSampledDataSource<TfToken>::New(
              HdTokens->rightHanded)),
      HdRetainedTypedSampledDataSource<TfToken>::New(
          PxOsdOpenSubdivTokens->none),
      nullptr,
      HdRetainedTypedSampledDataSource<bool>::New(true)));
  values.push_back(PrimvarsContainer(
      {HdTokens->points, HdTokens->displayColor, HdTokens->displayOpacity},
      {PointsPrimvar(pointsDS), ColorPrimvar(color), OpacityPrimvar(opacity)},
      extras));

  AppendTrailingSchemas(names, values, xform, lo, hi, materialPath);
  return HdRetainedContainerDataSource::New(
      names.size(), names.data(), values.data());
}

// Build the mesh face topology for triangle (index UINT32_VEC3) or quad (index
// UINT32_VEC4). `vertsPerFace` selects the subtype.
static void BuildIndexedFaceTopology(helium::Array1D *index,
    size_t numVerts,
    int vertsPerFace,
    VtIntArray &faceVertexCounts,
    VtIntArray &faceVertexIndices)
{
  if (index && index->size() > 0) {
    const size_t numFaces = index->size();
    const uint32_t *idx = static_cast<const uint32_t *>(index->data());
    faceVertexCounts.assign(numFaces, vertsPerFace);
    faceVertexIndices.resize(numFaces * vertsPerFace);
    for (size_t i = 0; i < numFaces * size_t(vertsPerFace); ++i)
      faceVertexIndices[i] = static_cast<int>(idx[i]);
  } else {
    const size_t numFaces = numVerts / vertsPerFace;
    faceVertexCounts.assign(numFaces, vertsPerFace);
    faceVertexIndices.resize(numFaces * vertsPerFace);
    for (size_t i = 0; i < numFaces * size_t(vertsPerFace); ++i)
      faceVertexIndices[i] = static_cast<int>(i);
  }
}

static HdContainerDataSourceHandle BuildIndexedMesh(Geometry *geom,
    int vertsPerFace,
    const GfVec3f &color,
    float opacity,
    const GfMatrix4d &xform,
    const SdfPath &materialPath,
    const InstanceBinding &instancing,
    const std::vector<DerivedPrimvar> &derived)
{
  helium::Array1D *posArray = geom->vertexPosition();
  const size_t numVerts = posArray->size();
  const GfVec3f *points = reinterpret_cast<const GfVec3f *>(posArray->data());
  auto pointsDS = ZeroCopyDataSource<GfVec3f>::New(posArray, points, numVerts);

  VtIntArray counts, indices;
  BuildIndexedFaceTopology(
      geom->index(), numVerts, vertsPerFace, counts, indices);

  GfVec3d lo, hi;
  ComputeExtent(points, numVerts, lo, hi);

  std::vector<uint32_t> primitives(counts.size());
  for (size_t i = 0; i < primitives.size(); ++i)
    primitives[i] = uint32_t(i);
  GeometryFrame frame;
  frame.points = points;
  frame.numPoints = numVerts;
  frame.xform = xform;
  auto extras = GatherOptionalPrimvars(geom,
      numVerts,
      indices.size(),
      nullptr,
      &primitives,
      instancing,
      derived,
      frame);
  return BuildMeshPrim(pointsDS,
      counts,
      indices,
      lo,
      hi,
      color,
      opacity,
      xform,
      materialPath,
      extras);
}

static HdContainerDataSourceHandle BuildTessellatedMesh(Geometry *geom,
    const TessMesh &mesh,
    const GfVec3f &color,
    float opacity,
    const GfMatrix4d &xform,
    const SdfPath &materialPath,
    const InstanceBinding &instancing,
    const std::vector<DerivedPrimvar> &derived)
{
  // Generated topology is copied (outside the zero-copy scope, SPEC §4.1).
  GeometryFrame frame;
  frame.points = mesh.points.data();
  frame.numPoints = mesh.points.size();
  frame.generatedNormals = mesh.normals.data();
  frame.numGeneratedNormals = mesh.normals.size();
  frame.xform = xform;
  auto extras = GatherOptionalPrimvars(geom,
      mesh.points.size(),
      0,
      &mesh.vertexIndices,
      &mesh.primitiveIndices,
      instancing,
      derived,
      frame);
  if (!mesh.normals.empty())
    extras.emplace_back(HdTokens->normals,
        ArrayPrimvar(
            HdRetainedTypedSampledDataSource<VtVec3fArray>::New(mesh.normals),
            HdPrimvarSchemaTokens->vertex,
            HdPrimvarSchemaTokens->normal));
  auto pointsDS =
      HdRetainedTypedSampledDataSource<VtVec3fArray>::New(mesh.points);
  GfVec3d lo, hi;
  ComputeExtent(mesh.points.data(), mesh.points.size(), lo, hi);
  return BuildMeshPrim(pointsDS,
      mesh.faceVertexCounts,
      mesh.faceVertexIndices,
      lo,
      hi,
      color,
      opacity,
      xform,
      materialPath,
      extras);
}

// --- curve widths ---------------------------------------------------------

// Widths data source (diameter = 2*radius), per-vertex when vertex.radius is
// present, else a single constant from the global radius.
static HdSampledDataSourceHandle BuildWidths(
    Geometry *geom, size_t numVerts, bool &perVertex)
{
  if (helium::Array1D *r = geom->vertexRadius()) {
    perVertex = true;
    const float *rad = static_cast<const float *>(r->data());
    VtFloatArray widths(std::min(numVerts, r->size()));
    for (size_t i = 0; i < widths.size(); ++i)
      widths[i] = 2.f * rad[i];
    return HdRetainedTypedSampledDataSource<VtFloatArray>::New(widths);
  }
  perVertex = false;
  return HdRetainedTypedSampledDataSource<float>::New(
      2.f * geom->globalRadius());
}

// --- basisCurves (curve) ---------------------------------------------------

static HdContainerDataSourceHandle BuildBasisCurvesPrim(Geometry *geom,
    const GfVec3f &color,
    float opacity,
    const GfMatrix4d &xform,
    const SdfPath &materialPath,
    const InstanceBinding &instancing,
    const std::vector<DerivedPrimvar> &derived)
{
  helium::Array1D *posArray = geom->vertexPosition();
  const size_t numVerts = posArray->size();
  const GfVec3f *points = reinterpret_cast<const GfVec3f *>(posArray->data());
  auto pointsDS = ZeroCopyDataSource<GfVec3f>::New(posArray, points, numVerts);

  bool perVertex = false;
  HdSampledDataSourceHandle widthsDS = BuildWidths(geom, numVerts, perVertex);

  // ANARI curve: primitive.index holds each segment's first vertex, so segment
  // i spans vertices (index[i], index[i]+1) — a two-vertex linear curve each
  // (SPEC §4.1). Without an index, keep one Hydra curve per ANARI segment.
  VtIntArray curveVertexCounts;
  VtIntArray curveIndices;
  std::vector<uint32_t> primitives;
  helium::Array1D *index = geom->index();
  if (index && index->size() > 0) {
    const uint32_t *idx = static_cast<const uint32_t *>(index->data());
    for (size_t i = 0; i < index->size(); ++i) {
      if (size_t(idx[i]) + 1 >= numVerts)
        continue; // malformed app index: skip the segment (as TessellateTubes)
      primitives.push_back(uint32_t(i));
      curveVertexCounts.push_back(2);
      curveIndices.push_back(int(idx[i]));
      curveIndices.push_back(int(idx[i]) + 1);
    }
  } else {
    for (size_t i = 0; i + 1 < numVerts; ++i) {
      primitives.push_back(uint32_t(i));
      curveVertexCounts.push_back(2);
      curveIndices.push_back(int(i));
      curveIndices.push_back(int(i + 1));
    }
  }

  GfVec3d lo, hi;
  ComputeExtent(points, numVerts, lo, hi);

  GeometryFrame frame;
  frame.points = points;
  frame.numPoints = numVerts;
  frame.xform = xform;

  std::vector<TfToken> names{
      HdBasisCurvesSchemaTokens->basisCurves, HdPrimvarsSchemaTokens->primvars};
  std::vector<HdDataSourceBaseHandle> values;
  values.push_back(HdBasisCurvesSchema::BuildRetained(
      HdBasisCurvesTopologySchema::BuildRetained(
          HdRetainedTypedSampledDataSource<VtIntArray>::New(curveVertexCounts),
          curveIndices.empty()
              ? nullptr
              : HdRetainedTypedSampledDataSource<VtIntArray>::New(curveIndices),
          HdRetainedTypedSampledDataSource<TfToken>::New(HdTokens->linear),
          HdRetainedTypedSampledDataSource<TfToken>::New(HdTokens->linear),
          HdRetainedTypedSampledDataSource<TfToken>::New(
              HdTokens->nonperiodic))));
  values.push_back(PrimvarsContainer({HdTokens->points,
                                         HdTokens->widths,
                                         HdTokens->displayColor,
                                         HdTokens->displayOpacity},
      {PointsPrimvar(pointsDS),
          WidthsPrimvar(widthsDS, perVertex),
          ColorPrimvar(color),
          OpacityPrimvar(opacity)},
      GatherOptionalPrimvars(geom,
          numVerts,
          0,
          nullptr,
          &primitives,
          instancing,
          derived,
          frame)));
  AppendTrailingSchemas(names, values, xform, lo, hi, materialPath);
  return HdRetainedContainerDataSource::New(
      names.size(), names.data(), values.data());
}

// --- dispatch --------------------------------------------------------------

// Derived primvars for the material's samplers (SPEC §6.4), one per distinct
// (sampler, width): baked st coordinates for image samplers on the baked path,
// values for transform and primitive samplers. An image sampler on an absent
// inAttribute gets no derived primvar: its reader is pointed at an unauthored
// primvar and falls back to (0,0) either way.
static std::vector<DerivedPrimvar> CollectDerivedPrimvars(Material *mat)
{
  std::vector<DerivedPrimvar> derived;
  if (!mat)
    return derived;
  for (const auto &b : mat->samplerBindings()) {
    const Sampler *s = b.sampler.ptr;
    if (!s || !s->authorsDerivedPrimvar())
      continue;
    DerivedPrimvar d;
    if (s->kind() == Sampler::Kind::Image) {
      d.name = TfToken(s->derivedStPrimvar());
      d.slot = s->inAttributeSlot();
      d.width = 2;
      d.transform = [s](const helium::float4 &a) {
        const helium::float2 t = Sampler::ToUsdSt(s->transformInput(a));
        return helium::float4(t.x, t.y, 0.f, 1.f);
      };
    } else {
      d.width = b.valueWidth();
      d.name = TfToken(s->derivedValuePrimvar(d.width));
      if (s->kind() == Sampler::Kind::Primitive) {
        d.primitive = s;
      } else {
        d.slot = s->inAttributeSlot();
        d.transform = [s](const helium::float4 &a) {
          return s->transformOutput(a);
        };
        d.constantWhenAbsent = true;
      }
    }
    if (std::any_of(derived.begin(),
            derived.end(),
            [&](const DerivedPrimvar &o) { return o.name == d.name; }))
      continue;
    derived.push_back(std::move(d));
  }
  return derived;
}

std::vector<InstancerAttribute> BuildDerivedInstanceAttributes(
    Material *material, const std::vector<InstanceOverrides> &overrides)
{
  std::vector<InstancerAttribute> out;
  for (const DerivedPrimvar &d : CollectDerivedPrimvars(material)) {
    if (d.primitive || !Sampler::IsAttributeSlot(d.slot))
      continue;
    const bool color = d.slot == Sampler::kSlotColor;
    if (std::none_of(overrides.begin(),
            overrides.end(),
            [&](const InstanceOverrides &o) {
              return color ? o.hasColor : o.hasAttribute[d.slot];
            }))
      continue;
    std::vector<helium::float4> values;
    for (const auto &o : overrides) {
      const helium::float4 &v = color ? o.color : o.attribute[d.slot];
      values.push_back(d.transform ? d.transform(v) : v);
    }
    VtValue array;
    if (d.width == 1) {
      VtFloatArray a(values.size());
      for (size_t i = 0; i < values.size(); ++i)
        a[i] = values[i].x;
      array = VtValue(a);
    } else if (d.width == 2) {
      VtVec2fArray a(values.size());
      for (size_t i = 0; i < values.size(); ++i)
        a[i] = GfVec2f(values[i].x, values[i].y);
      array = VtValue(a);
    } else {
      VtVec3fArray a(values.size());
      for (size_t i = 0; i < values.size(); ++i)
        a[i] = GfVec3f(values[i].x, values[i].y, values[i].z);
      array = VtValue(a);
    }
    out.emplace_back(d.name, std::move(array));
  }
  return out;
}

bool ReadsWorldSpace(Material *material)
{
  if (!material)
    return false;
  const auto &bindings = material->samplerBindings();
  return std::any_of(bindings.begin(), bindings.end(), [](const auto &b) {
    return b.sampler && b.sampler->readsWorldSpace();
  });
}

HdContainerDataSourceHandle BuildSurfacePrim(Surface *surface,
    const GfMatrix4d &xform,
    const SdfPath &materialPath,
    const InstanceBinding &instancing)
{
  Geometry *geom = surface->geometry();

  // displayColor fallback (SPEC §6.9): constant material color.
  Material *mat = surface->material();
  const float3 mc = mat ? mat->color() : float3(0.8f, 0.8f, 0.8f);
  const GfVec3f color(mc[0], mc[1], mc[2]);
  const float opacity = mat ? mat->opacity() : 1.f;

  // The rprim record carries the baked samplers' revisions so a sampler edit
  // re-authors this rprim (Mirror::MakeRecord). A slot some instance
  // overrides gets its values from the instancer
  // (BuildDerivedInstanceAttributes), which a constant here would shadow.
  std::vector<DerivedPrimvar> derived = CollectDerivedPrimvars(mat);
  for (auto &d : derived) {
    if (Sampler::IsAttributeSlot(d.slot)
        && (instancing.overrideMask & (1u << d.slot)))
      d.constantWhenAbsent = false;
  }

  HdContainerDataSourceHandle prim;
  if (const TessMesh *t = geom->tessellation())
    prim = BuildTessellatedMesh(
        geom, *t, color, opacity, xform, materialPath, instancing, derived);
  else {
    const TfToken pt = geom->primType();
    if (pt == HdPrimTypeTokens->basisCurves)
      prim = BuildBasisCurvesPrim(
          geom, color, opacity, xform, materialPath, instancing, derived);
    else
      // Mesh: triangle (3 verts/face) or quad (4 verts/face).
      prim = BuildIndexedMesh(geom,
          geom->meshVertsPerFace(),
          color,
          opacity,
          xform,
          materialPath,
          instancing,
          derived);
  }

  // Prototype of an instancer (SPEC §4.3): overlay an `instancedBy` on the base
  // rprim rather than thread it through every builder.
  if (!instancing.instancerPath.IsEmpty()) {
    auto instancedBy = HdInstancedBySchema::BuildRetained(
        HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(
            VtArray<SdfPath>({instancing.instancerPath})),
        HdRetainedTypedSampledDataSource<VtArray<SdfPath>>::New(
            VtArray<SdfPath>({instancing.prototypeRoot})));
    prim = HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdInstancedBySchema::GetSchemaToken(), instancedBy),
        prim);
  }
  return prim;
}

} // namespace anari_hydra
