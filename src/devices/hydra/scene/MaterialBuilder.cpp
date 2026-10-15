// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MaterialBuilder.h"
#include "NormalMappedSurface.h"
#include "sampler/Sampler.h"

#include <pxr/imaging/hd/materialConnectionSchema.h>
#include <pxr/imaging/hd/materialNetworkSchema.h>
#include <pxr/imaging/hd/materialNodeParameterSchema.h>
#include <pxr/imaging/hd/materialNodeSchema.h>
#include <pxr/imaging/hd/materialSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/glslfx.h>
#include <pxr/usd/sdr/registry.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/assetPath.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// The single shader node's key in the network, and its Sdr identifier. Storm
// ships a glslfx implementation of UsdPreviewSurface, so authoring it under the
// universal render context shades natively.
static const TfToken kNodeName("UsdPreviewSurface");
static const TfToken kNodeId("UsdPreviewSurface");
static const TfToken kSurfaceOutput("surface");

// --- generated normal-mapping surface ---------------------------------------
//
// See NormalMappedSurface.h for the source and why it exists.
static const TfToken kTangentStInput(kNormalMappedSurfaceTangentStInput);

// The Sdr identifier of the generated surface node, registered once.
static TfToken NormalMappedSurfaceNodeId()
{
  static const TfToken id = [] {
    SdrShaderNodeConstPtr node =
        SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(
            kNormalMappedSurfaceSource, HioGlslfxTokens->glslfx, SdrTokenMap());
    return node ? node->GetIdentifier() : TfToken();
  }();
  return id;
}

// Whether `b` is a transform/primitive sampler on the `normal` slot, which only
// the generated surface can honor.
static bool IsPrimvarNormalMap(const SamplerBinding &b)
{
  const Sampler *s = b.sampler.ptr;
  return b.normalMap && s && s->isValid() && s->kind() != Sampler::Kind::Image;
}

static HdTokenDataSourceHandle TypeName(const char *t)
{
  return HdRetainedTypedSampledDataSource<TfToken>::New(TfToken(t));
}

static HdTokenDataSourceHandle Tok(const TfToken &t)
{
  return HdRetainedTypedSampledDataSource<TfToken>::New(t);
}

static HdContainerDataSourceHandle FloatParam(float v)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<float>::New(v),
      nullptr,
      TypeName("float"));
}

static HdContainerDataSourceHandle IntParam(int v)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<int>::New(v), nullptr, TypeName("int"));
}

static HdContainerDataSourceHandle ColorParam(const float3 &c)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfVec3f>::New(GfVec3f(c[0], c[1], c[2])),
      nullptr,
      TypeName("color3f"));
}

static HdContainerDataSourceHandle TokenParam(const char *v)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<TfToken>::New(TfToken(v)),
      nullptr,
      TypeName("token"));
}

static HdContainerDataSourceHandle AssetParam(const std::string &path)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<SdfAssetPath>::New(SdfAssetPath(path)),
      nullptr,
      TypeName("asset"));
}

static HdContainerDataSourceHandle Vec4Param(const float4 &v)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfVec4f>::New(
          GfVec4f(v.x, v.y, v.z, v.w)),
      nullptr,
      TypeName("float4"));
}

static HdContainerDataSourceHandle Vec2Param(const helium::float2 &v)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfVec2f>::New(GfVec2f(v.x, v.y)),
      nullptr,
      TypeName("float2"));
}

// A single-connection input: input name -> [connection(upstreamNode, output)].
static HdDataSourceBaseHandle Connection(
    const TfToken &upstreamNode, const TfToken &output)
{
  std::vector<HdDataSourceBaseHandle> g{
      HdMaterialConnectionSchema::BuildRetained(
          Tok(upstreamNode), Tok(output))};
  return HdRetainedSmallVectorDataSource::New(g.size(), g.data());
}

// color3f UsdPreviewSurface inputs take a float3 primvar reader; every other
// (scalar) input takes a float reader.
static bool IsColorInput(const TfToken &usdInput)
{
  return usdInput == TfToken("diffuseColor")
      || usdInput == TfToken("emissiveColor");
}

static HdContainerDataSourceHandle Float3Param(const GfVec3f &v)
{
  return HdMaterialNodeParameterSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<GfVec3f>::New(v),
      nullptr,
      TypeName("float3"));
}

// One material network. `primvarNormalMap` selects the Storm-only variant: the
// generated surface node, with transform/primitive samplers on `normal` bound
// to it. Without it those bindings are left out (the universal network, for
// every other delegate and for Storm when the material has none).
static HdContainerDataSourceHandle BuildNetwork(
    Material *material, bool primvarNormalMap)
{
  const std::vector<SamplerBinding> &bindings = material->samplerBindings();
  const std::vector<AttributeBinding> &attrBindings =
      material->attributeBindings();

  // Which UsdPreviewSurface inputs are sampler- or attribute-driven: their
  // constant is omitted (the connection supplies the value).
  std::set<TfToken> bound;
  for (const auto &b : bindings)
    if (primvarNormalMap || !IsPrimvarNormalMap(b))
      bound.insert(b.usdInput);
  for (const auto &b : attrBindings)
    bound.insert(b.usdInput);

  // UsdPreviewSurface constant parameters. matte lands here too, with
  // metallic=0 / roughness=1 / emissive=0 defaults.
  std::vector<TfToken> pNames;
  std::vector<HdDataSourceBaseHandle> pValues;
  auto addConst = [&](const char *name, HdContainerDataSourceHandle v) {
    if (bound.count(TfToken(name)))
      return; // driven by a sampler connection
    pNames.push_back(TfToken(name));
    pValues.push_back(v);
  };
  addConst("diffuseColor", ColorParam(material->color()));
  addConst("opacity", FloatParam(material->opacity()));
  addConst("metallic", FloatParam(material->metallic()));
  addConst("roughness", FloatParam(material->roughness()));
  addConst("emissiveColor", ColorParam(material->emissive()));
  addConst("ior", FloatParam(material->ior()));
  addConst("clearcoat", FloatParam(material->clearcoat()));
  addConst("clearcoatRoughness", FloatParam(material->clearcoatRoughness()));
  addConst("occlusion", FloatParam(material->occlusion()));
  pNames.push_back(TfToken("useSpecularWorkflow"));
  pValues.push_back(IntParam(0));

  // Mask alpha uses an opacityThreshold; opaque/blend omit it.
  if (material->alphaMode() == Material::AlphaMode::Mask) {
    pNames.push_back(TfToken("opacityThreshold"));
    pValues.push_back(FloatParam(material->opacityThreshold()));
  }

  auto parameters = HdRetainedContainerDataSource::New(
      pNames.size(), pNames.data(), pValues.data());

  // Texture nodes: one UsdUVTexture per bound input, each fed by its own
  // UsdPrimvarReader_float2 on the primvar of the sampler's inAttribute.
  // Readers are per binding, not shared: Storm's material-network lowering
  // visits each reader node once, so a reader feeding two UsdTransform2d nodes
  // would leave the second without coordinates.
  std::vector<TfToken> nodeNames{kNodeName};
  std::vector<HdDataSourceBaseHandle> nodeValues; // filled after surface node

  std::vector<TfToken> surfConnNames;
  std::vector<HdDataSourceBaseHandle> surfConnValues;

  std::vector<std::pair<TfToken, HdDataSourceBaseHandle>> extraNodes;

  int idx = 0;
  for (const auto &b : bindings) {
    Sampler *s = b.sampler.ptr;
    if (!s || !s->isValid())
      continue;
    const std::string suffix = std::to_string(idx++);

    if (s->kind() != Sampler::Kind::Image) {
      // transform / primitive samplers have no texture: each bound rprim bakes
      // the sampler's value into a derived primvar (PrimBuilder), and the
      // network reads it with a typed UsdPrimvarReader — float3 for a color
      // slot, float (the first component, ANARI's scalar rule) for a scalar
      // one. On the `normal` slot only the generated surface reads it
      // (UsdPreviewSurface ignores a normal that is not a texture); its
      // fallback is the flat tangent-space normal.
      if (IsPrimvarNormalMap(b) && !primvarNormalMap)
        continue;
      const bool color = b.valueWidth() == 3;
      const std::string primvar = s->derivedValuePrimvar(b.valueWidth());
      const TfToken readerNode("valReader_" + b.usdInput.GetString() + suffix);
      std::vector<TfToken> rn{TfToken("varname")};
      std::vector<HdDataSourceBaseHandle> rv{TokenParam(primvar.c_str())};
      if (b.normalMap) {
        rn.push_back(TfToken("fallback"));
        rv.push_back(Float3Param(GfVec3f(0.f, 0.f, 1.f)));
      }
      auto rparams =
          HdRetainedContainerDataSource::New(rn.size(), rn.data(), rv.data());
      extraNodes.emplace_back(readerNode,
          HdMaterialNodeSchema::BuildRetained(rparams,
              nullptr,
              Tok(TfToken(color ? "UsdPrimvarReader_float3"
                                : "UsdPrimvarReader_float")),
              nullptr,
              nullptr));
      surfConnNames.push_back(b.usdInput);
      surfConnValues.push_back(Connection(readerNode, TfToken("result")));
      continue;
    }

    const TfToken texNode("tex_" + b.usdInput.GetString() + suffix);

    // Coordinate source. Baked input transforms read the derived per-rprim
    // primvar PrimBuilder authors for this sampler (already in USD st space);
    // otherwise the reader takes the attribute's own float2 primvar (`st` for
    // attribute0) and a UsdTransform2d applies the sampler's rotation·scale +
    // offset. UsdUVTexture has no st-transform input, so the UsdTransform2d is
    // the one target for it (the texture's own scale/bias carry outTransform
    // instead). Geometry-derived sources (position / normal / primitiveId)
    // always take the baked path. An unknown inAttribute reads a primvar no
    // rprim authors, so on either path the reader's (0,0) fallback applies,
    // exactly as for an attributeN the geometry does not provide.
    const bool baked = s->inputMode() == Sampler::InputMode::Baked;
    std::string primvar;
    if (s->inAttributeSlot() == Sampler::kAbsentAttributeSlot)
      primvar = Sampler::kAbsentAttributePrimvar;
    else if (baked)
      primvar = s->derivedStPrimvar();
    else if (s->inAttributeSlot() == 0)
      primvar = "st";
    else
      primvar = s->inAttribute() + "_float2";
    const TfToken readerNode("stReader_" + b.usdInput.GetString() + suffix);
    auto rparams = HdRetainedContainerDataSource::New(
        TfToken("varname"), TokenParam(primvar.c_str()));
    extraNodes.emplace_back(readerNode,
        HdMaterialNodeSchema::BuildRetained(rparams,
            nullptr,
            Tok(TfToken("UsdPrimvarReader_float2")),
            nullptr,
            nullptr));

    TfToken stSource = readerNode;
    const TfToken stOutput("result");
    if (!baked) {
      // ANARI: t' = R(θ)·(s⊙t) + b, with v=0 addressing image row zero (top).
      // Hio uploads that row at USD v=1, so the node evaluates F·t' + (0,1)
      // with F = diag(1,-1). Since F·R(θ) = R(−θ)·F, Storm's
      // `translation + R(rot)·(scale⊙in)` takes rot = −θ, scale = (sx, −sy),
      // translation = (bx, 1−by).
      const helium::float2 anariScale = s->stScale();
      const helium::float2 anariBias = s->stBias();
      const helium::float2 sc(anariScale.x, -anariScale.y);
      const helium::float2 bi(anariBias.x, 1.f - anariBias.y);
      const TfToken xfmNode("stXform_" + b.usdInput.GetString() + suffix);
      std::vector<TfToken> xn{
          TfToken("rotation"), TfToken("scale"), TfToken("translation")};
      std::vector<HdDataSourceBaseHandle> xv{
          FloatParam(-s->stRotationDegrees()), Vec2Param(sc), Vec2Param(bi)};
      auto xparams =
          HdRetainedContainerDataSource::New(xn.size(), xn.data(), xv.data());
      auto xconns = HdRetainedContainerDataSource::New(
          TfToken("in"), Connection(readerNode, stOutput));
      extraNodes.emplace_back(xfmNode,
          HdMaterialNodeSchema::BuildRetained(xparams,
              xconns,
              Tok(TfToken("UsdTransform2d")),
              nullptr,
              nullptr));
      stSource = xfmNode;
    }

    // UsdUVTexture: file + wrap + output scale/bias + sourceColorSpace raw
    // (the sampler decodes to linear). scale/bias carry the sampler's diagonal
    // ANARI outTransform (a channel-mixing outTransform is already baked into
    // the sampler's texels and reports identity here). The `normal` slot adds
    // nothing: per ANARI the app's outTransform/outOffset already decodes the
    // texels to [-1,1]. raw is already correct — a normal map must not be
    // sRGB-decoded. Scalar slots read the red channel (the first component of
    // the transformed texel, ANARI's scalar-from-sampler rule).
    std::vector<TfToken> tn{TfToken("file"),
        TfToken("wrapS"),
        TfToken("wrapT"),
        TfToken("sourceColorSpace"),
        TfToken("scale"),
        TfToken("bias")};
    std::vector<HdDataSourceBaseHandle> tv{AssetParam(s->assetPath()),
        TokenParam(s->wrapS()),
        TokenParam(s->wrapT()),
        TokenParam("raw"),
        Vec4Param(s->outScale()),
        Vec4Param(s->outBias())};

    // filter -> Storm magFilter/minFilter. "nearest" authors both as plain
    // nearest — no mip selection, so texel values are exactly the stored
    // checker (the least-surprise reading of ANARI's `filter`). "linear"
    // authors nothing: Storm already defaults to linear/linearMipmapLinear, so
    // omitting keeps that default and the material network minimal. Applies to
    // color and normal-map bindings alike (both come from a sampler object).
    if (s->filterIsNearest()) {
      tn.push_back(TfToken("magFilter"));
      tv.push_back(TokenParam("nearest"));
      tn.push_back(TfToken("minFilter"));
      tv.push_back(TokenParam("nearest"));
    }
    auto tparams =
        HdRetainedContainerDataSource::New(tn.size(), tn.data(), tv.data());
    auto tconns = HdRetainedContainerDataSource::New(
        TfToken("st"), Connection(stSource, stOutput));
    extraNodes.emplace_back(texNode,
        HdMaterialNodeSchema::BuildRetained(
            tparams, tconns, Tok(TfToken("UsdUVTexture")), nullptr, nullptr));

    surfConnNames.push_back(b.usdInput);
    surfConnValues.push_back(Connection(texNode, b.output));
  }

  // Attribute nodes: one UsdPrimvarReader_* per attribute-bound input, reading
  // the per-vertex primvar PrimBuilder authors for that ANARI attribute
  // (color/attribute0..3). A color3f input takes a float3 reader; a scalar
  // input takes a float reader. The reader returns its `fallback` when the
  // geometry lacks the primvar.
  int aidx = 0;
  for (const auto &b : attrBindings) {
    const bool isColor = IsColorInput(b.usdInput);
    const TfToken readerNode(
        "attr_" + b.usdInput.GetString() + std::to_string(aidx++));
    std::vector<TfToken> rn{TfToken("varname")};
    const std::string primvar = b.primvar + (isColor ? "_float3" : "_float1");
    std::vector<HdDataSourceBaseHandle> rv{TokenParam(primvar.c_str())};
    auto rparams =
        HdRetainedContainerDataSource::New(rn.size(), rn.data(), rv.data());
    extraNodes.emplace_back(readerNode,
        HdMaterialNodeSchema::BuildRetained(rparams,
            nullptr,
            Tok(TfToken(isColor ? "UsdPrimvarReader_float3"
                                : "UsdPrimvarReader_float")),
            nullptr,
            nullptr));
    surfConnNames.push_back(b.usdInput);
    surfConnValues.push_back(Connection(readerNode, TfToken("result")));
  }

  // The generated surface's tangent frame comes from the geometry's `st`
  // (attribute0), read like any primvar so a geometry without it falls back
  // to a constant (0,0) and the map has no effect.
  if (primvarNormalMap) {
    const TfToken readerNode("tangentStReader");
    auto rparams = HdRetainedContainerDataSource::New(
        TfToken("varname"), TokenParam("st"));
    extraNodes.emplace_back(readerNode,
        HdMaterialNodeSchema::BuildRetained(rparams,
            nullptr,
            Tok(TfToken("UsdPrimvarReader_float2")),
            nullptr,
            nullptr));
    surfConnNames.push_back(kTangentStInput);
    surfConnValues.push_back(Connection(readerNode, TfToken("result")));
  }

  HdContainerDataSourceHandle surfConns;
  if (!surfConnNames.empty()) {
    surfConns = HdRetainedContainerDataSource::New(
        surfConnNames.size(), surfConnNames.data(), surfConnValues.data());
  }

  auto surfaceNode = HdMaterialNodeSchema::BuildRetained(parameters,
      /* inputConnections = */ surfConns,
      HdRetainedTypedSampledDataSource<TfToken>::New(
          primvarNormalMap ? NormalMappedSurfaceNodeId() : kNodeId),
      /* renderContextNodeIdentifiers = */ nullptr,
      /* nodeTypeInfo = */ nullptr);
  nodeValues.push_back(surfaceNode);

  for (auto &n : extraNodes) {
    nodeNames.push_back(n.first);
    nodeValues.push_back(n.second);
  }

  auto nodes = HdRetainedContainerDataSource::New(
      nodeNames.size(), nodeNames.data(), nodeValues.data());

  // surface terminal -> the UsdPreviewSurface node's "surface" output.
  auto terminal = HdMaterialConnectionSchema::BuildRetained(
      HdRetainedTypedSampledDataSource<TfToken>::New(kNodeName),
      HdRetainedTypedSampledDataSource<TfToken>::New(kSurfaceOutput));
  auto terminals = HdRetainedContainerDataSource::New(
      HdMaterialSchemaTokens->surface, terminal);

  return HdMaterialNetworkSchema::BuildRetained(
      nodes, terminals, /* interface = */ nullptr, /* config = */ nullptr);
}

HdContainerDataSourceHandle BuildMaterialPrim(Material *material)
{
  // The universal render context carries the UsdPreviewSurface network every
  // delegate reads. A transform/primitive sampler on `normal` adds the
  // generated-surface network under `glslfx`, which Storm prefers and no other
  // delegate reads; every other material authors one network.
  const auto &bindings = material->samplerBindings();
  bool primvarNormalMap =
      std::any_of(bindings.begin(), bindings.end(), IsPrimvarNormalMap);
  if (primvarNormalMap && NormalMappedSurfaceNodeId().IsEmpty()) {
    material->reportMessage(ANARI_SEVERITY_WARNING,
        "physicallyBased 'normal': the generated normal-mapping shader failed "
        "to register; the transform/primitive sampler is dropped");
    primvarNormalMap = false;
  }

  std::vector<TfToken> contexts{HdMaterialSchemaTokens->universalRenderContext};
  std::vector<HdDataSourceBaseHandle> networks{BuildNetwork(material, false)};
  if (primvarNormalMap) {
    contexts.push_back(HioGlslfxTokens->glslfx);
    networks.push_back(BuildNetwork(material, true));
  }
  auto materialSchema = HdMaterialSchema::BuildRetained(
      contexts.size(), contexts.data(), networks.data());

  return HdRetainedContainerDataSource::New(
      HdMaterialSchemaTokens->material, materialSchema);
}

} // namespace anari_hydra
