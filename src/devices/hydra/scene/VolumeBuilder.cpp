// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "VolumeBuilder.h"
#include "spatial_field/SpatialField.h"
#include "volume/Volume.h"

#include <pxr/imaging/hd/extentSchema.h>
#include <pxr/imaging/hd/instancedBySchema.h>
#include <pxr/imaging/hd/materialBindingSchema.h>
#include <pxr/imaging/hd/materialBindingsSchema.h>
#include <pxr/imaging/hd/materialConnectionSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialNodeParameterSchema.h>
#include <pxr/imaging/hd/materialNodeSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/purposeSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/visibilitySchema.h>
#include <pxr/imaging/hd/volumeFieldBindingSchema.h>
#include <pxr/imaging/hd/volumeFieldSchema.h>
#include <pxr/imaging/hd/xformSchema.h>
#include <pxr/imaging/hio/glslfx.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdr/registry.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// The volume's field binding names, which are also the shader inputs they
// feed: the ANARI scalar field (`value`) and the volume's classification
// table (`tf`, BuildClassificationTable).
static const TfToken kValue("value");
static const TfToken kTable("tf");
static const TfToken kShaderNode("anariVolume");
static const TfToken kReaderNode("anariValueReader");
static const TfToken kTableReaderNode("anariTableReader");

const TfToken &FieldPrimType()
{
  // Storm's field bprim types are openvdbAsset/field3dAsset (HdStField); the
  // texture loader dispatches on the asset's extension, not the prim type.
  static const TfToken t("openvdbAsset");
  return t;
}

template <typename T>
static typename HdRetainedTypedSampledDataSource<T>::Handle Retained(const T &v)
{
  return HdRetainedTypedSampledDataSource<T>::New(v);
}

HdContainerDataSourceHandle BuildFieldPrim(const FieldSnapshot &field)
{
  // Storm reads the *resolved* path (HdStField::Sync), so author it resolved.
  const std::string &path = field.assetPath;
  auto volumeField =
      HdVolumeFieldSchema::BuildRetained(Retained(SdfAssetPath(path, path)),
          Retained(kValue),
          Retained(0),
          Retained(TfToken("float")),
          nullptr);
  return HdRetainedContainerDataSource::New(
      HdVolumeFieldSchemaTokens->volumeField, volumeField);
}

// --- classification table ----------------------------------------------------

// Table layout, one float per texel, x = entry index, y = row:
//   rows 0-3  color r, g, b, a  (numColors entries)
//   row  4    opacity           (numOpacities entries)
//   row  5    scalars, indexed by the enum below.
// The shader (VolumeShaderSource) reads it with texelFetch, so the layout is
// a contract between the two.
enum TableScalar
{
  kNumColors,
  kNumOpacities,
  kValueLo,
  kInvValueRange,
  kInvUnitDistance,
  kDomainLoX,
  kDomainLoY,
  kDomainLoZ,
  kDomainHiX,
  kDomainHiY,
  kDomainHiZ,
  kNumScalars
};
static constexpr int kColorRow = 0;
static constexpr int kOpacityRow = 4;
static constexpr int kScalarRow = 5;
static constexpr int kTableRows = 6;

// A finite float (the shader's arithmetic must not see inf/nan).
static float Finite(float v)
{
  return std::isfinite(v) ? v
                          : (v > 0.f ? 3.4e38f : (v < 0.f ? -3.4e38f : 0.f));
}

std::shared_ptr<AnariFieldData> BuildClassificationTable(
    const VolumeClassification &c, const FieldSnapshot &field)
{
  const size_t nc = c.colors.size(), no = c.opacities.size();
  const int w = int(std::max({nc, no, size_t(kNumScalars)}));
  auto t = std::make_shared<AnariFieldData>();
  t->dims = GfVec3i(w, kTableRows, 1);
  t->values.assign(size_t(w) * kTableRows, 0.f);
  auto at = [&](int i, int row) -> float & {
    return t->values[size_t(row) * w + i];
  };
  for (size_t i = 0; i < nc; ++i)
    for (int k = 0; k < 4; ++k)
      at(int(i), kColorRow + k) = c.colors[i][k];
  for (size_t i = 0; i < no; ++i)
    at(int(i), kOpacityRow) = c.opacities[i];

  const Bounds domain = field.bounds();
  const float range = c.valueHi - c.valueLo;
  float scalars[kNumScalars];
  scalars[kNumColors] = float(nc);
  scalars[kNumOpacities] = float(no);
  scalars[kValueLo] = c.valueLo;
  scalars[kInvValueRange] = range != 0.f ? 1.f / range : 0.f;
  scalars[kInvUnitDistance] = 1.f / c.unitDistance;
  for (int k = 0; k < 3; ++k) {
    scalars[kDomainLoX + k] = domain.GetMin()[k];
    scalars[kDomainHiX + k] = domain.GetMax()[k];
  }
  for (int i = 0; i < kNumScalars; ++i)
    at(i, kScalarRow) = Finite(scalars[i]);

  const Bounds proxy = field.proxyBounds();
  t->table = true;
  t->boxLo = proxy.GetMin();
  t->boxHi = proxy.GetMax();
  return t;
}

// --- shader
// --------------------------------------------------------------------

// One source for every volume: everything an edit can change is in the
// volume's classification table, bound as a second field named "tf". Sdr
// therefore registers exactly one node and Storm compiles one program, however
// many classification edits are made (019-5; Sdr never frees nodes).
//
// OpenUSD coupling: `HdGetSampler_tf_texture()` and `HD_HAS_tf_texture` are
// Storm codegen internals (the field's binding name plus
// HdSt_ResourceBindingSuffixTokens->texture, hdSt/codeGen.cpp), used because
// Storm's own field accessor HdGet_tf(vec3) samples in eye space and cannot
// fetch an exact texel. Re-check them on an OpenUSD upgrade (README.md).
static std::string VolumeShaderSource()
{
  std::ostringstream s;
  s << "-- glslfx version 0.1\n"
       "-- configuration\n"
       "{\n"
       "  \"techniques\": { \"default\": { \"volumeShader\": {\n"
       "    \"source\": [ \"AnariTransferFunction1D.VolumeShader\" ] } } },\n"
       "  \"parameters\": {\n"
       "    \"value\": { \"default\": 0.0, \"type\": \"float\" },\n"
       "    \"tf\": { \"default\": 0.0, \"type\": \"float\" }\n"
       "  }\n"
       "}\n"
       "-- glsl AnariTransferFunction1D.VolumeShader\n"
       // Defined by Storm's volume ray marcher (hdSt volume.glslfx): eye space
       // to the volume prim's local (ANARI object) space.
       "vec3 coordsToLocalVolumeSpace(vec3 coords);\n";

  s << R"GLSL(
// Entry i of row `row` of the classification table (BuildClassificationTable
// layout: rows 0-3 color rgba, row 4 opacity, row 5 scalars in TableScalar
// order). Without the table (never, in practice) everything reads 0, which
// classifies as fully transparent.
float anariTable(int i, int row)
{
#if defined(HD_HAS_tf_texture)
    return texelFetch(HdGetSampler_tf_texture(), ivec3(i, row, 0), 0).x;
#else
    return 0.0;
#endif
}

int anariNumColors = 1;
int anariNumOpacities = 1;
float anariValueLo = 0.0;
float anariInvValueRange = 0.0;
float anariInvUnitDistance = 1.0;
vec3 anariDomainLo = vec3(0.0);
vec3 anariDomainHi = vec3(0.0);

void anariLoadScalars()
{
    anariNumColors = max(int(anariTable(0, 5)), 1);
    anariNumOpacities = max(int(anariTable(1, 5)), 1);
    anariValueLo = anariTable(2, 5);
    anariInvValueRange = anariTable(3, 5);
    anariInvUnitDistance = anariTable(4, 5);
    anariDomainLo = vec3(anariTable(5, 5), anariTable(6, 5), anariTable(7, 5));
    anariDomainHi = vec3(anariTable(8, 5), anariTable(9, 5), anariTable(10, 5));
}

vec4 anariColorEntry(int i)
{
    return vec4(anariTable(i, 0), anariTable(i, 1), anariTable(i, 2),
                anariTable(i, 3));
}

// Linear interpolation of a table spread uniformly over t in [0, 1].
vec4 anariColorAt(float t)
{
    const float x = t * float(anariNumColors - 1);
    const int i = min(int(floor(x)), anariNumColors - 1);
    const int j = min(i + 1, anariNumColors - 1);
    return mix(anariColorEntry(i), anariColorEntry(j), x - float(i));
}

float anariOpacityAt(float t)
{
    const float x = t * float(anariNumOpacities - 1);
    const int i = min(int(floor(x)), anariNumOpacities - 1);
    const int j = min(i + 1, anariNumOpacities - 1);
    return mix(anariTable(i, 4), anariTable(j, 4), x - float(i));
}

// Per-fragment constants of Storm's ray march (hdSt volume.glslfx main()),
// recomputed here because the material shader precedes the ray marcher in
// the generated source and cannot read its globals.
bool anariReady = false;
mat3 anariEyeToLocal;   // linear part of eye -> volume-local
mat3 anariLocalToEye;
float anariStepEye;     // Storm's resolvedStepSizeEye
float anariStepWorld;   // Storm's resolvedStepSizeWorld
vec4 anariInvProjDir;   // inverse(projection) * (0, 0, 1, 0)
vec3 anariCachedP = vec3(0.0);
vec4 anariCached = vec4(0.0);
bool anariHasCache = false;

float anariMaxColumn(mat3 m)
{
    return max(length(m[0]), max(length(m[1]), length(m[2])));
}

void anariInit(vec3 p)
{
    if (anariReady)
        return;
    anariReady = true;
    anariLoadScalars();
    const vec3 o = coordsToLocalVolumeSpace(p);
    anariEyeToLocal = mat3(coordsToLocalVolumeSpace(p + vec3(1.0, 0.0, 0.0)) - o,
                           coordsToLocalVolumeSpace(p + vec3(0.0, 1.0, 0.0)) - o,
                           coordsToLocalVolumeSpace(p + vec3(0.0, 0.0, 1.0)) - o);
    anariLocalToEye = inverse(anariEyeToLocal);
    anariStepEye = float(HdGet_stepSize()) * 0.5 * float(HdGet_sampleDistance())
        / anariMaxColumn(anariEyeToLocal);
    const mat4 viewInverse = mat4(GetWorldToViewInverseMatrix());
    anariStepWorld = anariMaxColumn(mat3(viewInverse)) * anariStepEye;
    anariInvProjDir = vec4(inverse(mat4(GetProjectionMatrix()))[2]);
}

// The ray interval Storm's sample at eye point p stands for (one step,
// centred on p), clipped to the ANARI field domain in volume-local (ANARI
// object) space: rgb = classified color at the clipped interval's midpoint,
// a = its optical depth (extinction per unitDistance of object space times
// the object-space length inside the domain). Clipping keeps Storm's proxy
// box, half a voxel larger than the domain, from adding coverage, and makes
// the per-step sums exact for a constant field.
vec4 anariInterval(vec3 p)
{
    if (anariHasCache && p == anariCachedP)
        return anariCached;
    anariInit(p);
    anariHasCache = true;
    anariCachedP = p;
    anariCached = vec4(0.0);

    // Storm's computeRayDirectionEye.
    const vec3 dirEye =
        normalize(anariInvProjDir.xyz - p * anariInvProjDir.w);
    const vec3 lp = coordsToLocalVolumeSpace(p);
    const vec3 d = anariEyeToLocal * (dirEye * anariStepEye);
    const vec3 a = lp - 0.5 * d;
    float t0 = 0.0;
    float t1 = 1.0;
    for (int k = 0; k < 3; ++k) {
        if (abs(d[k]) < 1e-20) {
            if (a[k] < anariDomainLo[k] || a[k] > anariDomainHi[k])
                return anariCached;
        } else {
            const float ta = (anariDomainLo[k] - a[k]) / d[k];
            const float tb = (anariDomainHi[k] - a[k]) / d[k];
            t0 = max(t0, min(ta, tb));
            t1 = min(t1, max(ta, tb));
        }
    }
    if (t1 <= t0)
        return anariCached;

    const vec3 mid = a + (0.5 * (t0 + t1)) * d;
    const float s = HdGet_value(p + anariLocalToEye * (mid - lp));
    if (isnan(s))
        return anariCached;
    const float t = clamp((s - anariValueLo) * anariInvValueRange, 0.0, 1.0);
    const vec4 c = anariColorAt(t);
    // ANARI final opacity = color.a * opacity; per-unitDistance opacity to
    // extinction as VisRTX (the ceiling keeps log finite).
    const float alpha = clamp(c.a * anariOpacityAt(t), 0.0, 0.99999994);
    const float sigma = -log(1.0 - alpha) * anariInvUnitDistance;
    anariCached = vec4(c.rgb, sigma * (t1 - t0) * length(d));
    return anariCached;
}

// Storm integrates T *= exp(-extinction * step), so extinction is the
// interval's optical depth per world step.
float extinctionFunction(vec3 p)
{
    return anariInterval(p).a / anariStepWorld;
}

// Emission-absorption only: no in-scattering, so scene lights do not shade
// the volume (README.md).
float scatteringFunction(vec3 p)
{
    return 0.0;
}

// Storm adds step * T * emission; this makes each interval contribute
// T * color * (1 - exp(-depth)), exact alpha compositing of the interval.
vec3 emissionFunction(vec3 p)
{
    const vec4 i = anariInterval(p);
    return i.rgb * ((1.0 - exp(-i.a)) / anariStepWorld);
}

float phaseFunction(vec3 direction1, vec3 direction2)
{
    return 1.0 / (4.0 * 3.14159265358979);
}
)GLSL";
  return s.str();
}

// --- material ---------------------------------------------------------------

static HdTokenDataSourceHandle Tok(const TfToken &t)
{
  return HdRetainedTypedSampledDataSource<TfToken>::New(t);
}

// HwFieldReader_float (usdHydra shaderDefs, role `field`) reading the field
// bound as `name`: makes Storm allocate that field's texture and emit
// HdGet_<name>(vec3).
static HdDataSourceBaseHandle FieldReaderNode(const TfToken &name)
{
  static const TfToken fieldname("fieldname");
  HdDataSourceBaseHandle param = HdMaterialNodeParameterSchema::BuildRetained(
      Tok(name), nullptr, Tok(TfToken("token")));
  return HdMaterialNodeSchema::BuildRetained(
      HdRetainedContainerDataSource::New(fieldname, param),
      nullptr,
      Tok(TfToken("HwFieldReader_float")),
      nullptr,
      nullptr);
}

static HdDataSourceBaseHandle ConnectionTo(const TfToken &node)
{
  HdDataSourceBaseHandle c = HdMaterialConnectionSchema::BuildRetained(
      Tok(node), Tok(TfToken("result")));
  return HdRetainedSmallVectorDataSource::New(1, &c);
}

HdContainerDataSourceHandle BuildVolumeMaterialPrim()
{
  // The source never changes, so this registers one Sdr node per process.
  SdrShaderNodeConstPtr node =
      SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(
          VolumeShaderSource(), HioGlslfxTokens->glslfx, SdrTokenMap());
  const TfToken nodeId = node ? node->GetIdentifier() : TfToken();

  TfToken inNames[] = {kValue, kTable};
  HdDataSourceBaseHandle inValues[] = {
      ConnectionTo(kReaderNode), ConnectionTo(kTableReaderNode)};
  auto shaderNode =
      HdMaterialNodeSchema::BuildRetained(HdRetainedContainerDataSource::New(),
          HdRetainedContainerDataSource::New(2, inNames, inValues),
          Tok(nodeId),
          nullptr,
          nullptr);

  TfToken nodeNames[] = {kShaderNode, kReaderNode, kTableReaderNode};
  HdDataSourceBaseHandle nodeValues[] = {
      shaderNode, FieldReaderNode(kValue), FieldReaderNode(kTable)};
  auto nodes = HdRetainedContainerDataSource::New(3, nodeNames, nodeValues);

  auto terminal = HdMaterialConnectionSchema::BuildRetained(
      Tok(kShaderNode), Tok(HdMaterialTerminalTokens->volume));
  auto terminals = HdRetainedContainerDataSource::New(
      HdMaterialTerminalTokens->volume, terminal);

  auto network = HdMaterialNetworkSchema::BuildRetained(
      nodes, terminals, nullptr, nullptr);
  const TfToken ctx = HdMaterialSchemaTokens->universalRenderContext;
  HdDataSourceBaseHandle netVal = network;
  return HdRetainedContainerDataSource::New(HdMaterialSchemaTokens->material,
      HdMaterialSchema::BuildRetained(1, &ctx, &netVal));
}

// --- volume rprim
// -------------------------------------------------------------

HdContainerDataSourceHandle BuildVolumePrim(const VolumeClassification &c,
    const FieldSnapshot &field,
    const SdfPath &fieldPath,
    const SdfPath &tablePath,
    const SdfPath &materialPath,
    const SdfPath &instancerPath,
    const SdfPath &prototypeRoot)
{
  std::vector<TfToken> names;
  std::vector<HdDataSourceBaseHandle> values;

  TfToken bindNames[] = {kValue, kTable};
  HdDataSourceBaseHandle fieldBinding[] = {
      Retained(fieldPath), Retained(tablePath)};
  names.push_back(HdVolumeFieldBindingSchema::GetSchemaToken());
  values.push_back(
      HdVolumeFieldBindingSchema::BuildRetained(2, bindNames, fieldBinding));

  const TfToken allPurpose = HdMaterialBindingsSchemaTokens->allPurpose;
  HdDataSourceBaseHandle binding =
      HdMaterialBindingSchema::BuildRetained(Retained(materialPath));
  names.push_back(HdMaterialBindingsSchema::GetSchemaToken());
  values.push_back(
      HdMaterialBindingsSchema::BuildRetained(1, &allPurpose, &binding));

  names.push_back(HdPurposeSchemaTokens->purpose);
  values.push_back(HdPurposeSchema::BuildRetained(
      Retained(HdRenderTagTokens->geometry), nullptr, nullptr));

  names.push_back(HdVisibilitySchemaTokens->visibility);
  values.push_back(HdVisibilitySchema::BuildRetained(Retained(c.visible)));

  // World-direct volumes and prototypes alike sit at identity: ANARI volumes
  // carry no transform of their own (instances supply it).
  names.push_back(HdXformSchemaTokens->xform);
  values.push_back(
      HdXformSchema::BuildRetained(Retained(GfMatrix4d(1.0)), Retained(false)));

  // Storm culls and rasterizes the proxy box (half a voxel past the domain).
  const Bounds b = field.proxyBounds();
  names.push_back(HdExtentSchemaTokens->extent);
  values.push_back(HdExtentSchema::BuildRetained(
      Retained(GfVec3d(b.GetMin())), Retained(GfVec3d(b.GetMax()))));

  if (!instancerPath.IsEmpty()) {
    names.push_back(HdInstancedBySchema::GetSchemaToken());
    values.push_back(HdInstancedBySchema::BuildRetained(
        Retained(VtArray<SdfPath>({instancerPath})),
        Retained(VtArray<SdfPath>({prototypeRoot}))));
  }

  return HdRetainedContainerDataSource::New(
      names.size(), names.data(), values.data());
}

} // namespace anari_hydra
