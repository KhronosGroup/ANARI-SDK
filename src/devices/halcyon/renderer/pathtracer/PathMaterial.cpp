// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "PathMaterial.h"
#include "ClosureAlbedo.h"
#include "ClosureClassification.h"
#include "ClosureSampling.h"
#include "ColorManagement.h"
#include "ContextGroups.h"
// typhoon
#include <renderer/materials/MaterialXCpp/materials/adobeOpenPbr.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/spectral.h>
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace halcyon::pathtracer {

// CppMaterial ////////////////////////////////////////////////////////////////

// PathShadeInSoA::attributes has a row per attribute before ATTRIBUTE_NONE
static_assert(sizeof(ispc::PathShadeInSoA::attributes)
        / sizeof(ispc::PathShadeInSoA::attributes[0])
    == size_t(ispc::ATTRIBUTE_NONE));

namespace {

// By ispc::Attribute
const char *const ATTRIBUTE_NAMES[] = {"attribute0",
    "attribute1",
    "attribute2",
    "attribute3",
    "color",
    "worldPosition",
    "worldNormal",
    "objectPosition",
    "objectNormal"};
static_assert(std::size(ATTRIBUTE_NAMES) == size_t(ispc::ATTRIBUTE_NONE));

} // namespace

int attributeFromName(const std::string &name)
{
  for (int i = 0; i < int(std::size(ATTRIBUTE_NAMES)); i++) {
    if (name == ATTRIBUTE_NAMES[i])
      return i;
  }
  return -1;
}

const char *attributeName(int attribute)
{
  return attribute >= 0 && attribute < int(std::size(ATTRIBUTE_NAMES))
      ? ATTRIBUTE_NAMES[attribute]
      : nullptr;
}

namespace {

const std::string GEOMPROP_NODE_PREFIX = "ND_geompropvalue_";
// Joins a geomprop's name and its node's type in the handle table
constexpr char GEOMPROP_TYPE_SEPARATOR = '#';

GeomPropType geomPropType(const std::string &type)
{
  if (type == "float")
    return GeomPropType::FLOAT;
  if (type == "vector2")
    return GeomPropType::VECTOR2;
  if (type == "vector3" || type == "color3")
    return GeomPropType::VECTOR3;
  if (type == "vector4" || type == "color4")
    return GeomPropType::VECTOR4;
  if (type == "integer")
    return GeomPropType::INTEGER;
  if (type == "boolean")
    return GeomPropType::BOOLEAN;
  return GeomPropType::UNSUPPORTED;
}

} // namespace

CppMaterial::CppMaterial(std::unique_ptr<mxcpp::EvalGraph> graph,
    std::vector<GeomPropBinding> geomProps,
    std::vector<const void *> samplers,
    ImageFiles images,
    uint32_t contextMask)
    : m_graph(std::move(graph)),
      m_geomProps(std::move(geomProps)),
      m_textures(std::move(samplers), std::move(images)),
      m_contextMask(contextMask)
{
  for (const GeomPropBinding &b : m_geomProps) {
    if (b.attribute >= 0 && b.type != GeomPropType::UNSUPPORTED)
      m_attributeMask |= 1u << b.attribute;
  }
}

std::unique_ptr<CppMaterial> CppMaterial::compile(
    const mxcpp::MaterialGraph &graph,
    std::vector<const void *> samplers,
    std::string *diagnostic,
    ImageFiles images)
{
  registerTextureNodes();

  // A geomprop handle's lookup returns one value type, so give each node
  // type reading a name its own handle: the name and the type
  mxcpp::MaterialGraph typed = graph;
  for (auto &entry : typed.nodes) {
    mxcpp::GraphNode &node = entry.second;
    if (node.nodeTypeId.compare(
            0, GEOMPROP_NODE_PREFIX.size(), GEOMPROP_NODE_PREFIX)
        != 0)
      continue;
    auto it = node.parameters.find("geomprop");
    if (it == node.parameters.end()
        || !mxcpp::ValueHolds<std::string>(it->second))
      continue;
    it->second = mxcpp::Value(mxcpp::ValueGet<std::string>(it->second)
        + GEOMPROP_TYPE_SEPARATOR
        + node.nodeTypeId.substr(GEOMPROP_NODE_PREFIX.size()));
  }

  const std::vector<std::string> names = mxcpp::CollectGeomPropNames(typed);
  mxcpp::CompileResult result =
      mxcpp::EvalGraph::Compile(typed, "surface", names);
  if (diagnostic)
    *diagnostic = result.diagnostic;
  if (!result.graph || !result.graph->IsValid())
    return {};

  // Typhoon re-interpolates the object position for graphs with an
  // object-space position node, Compile()'s own included
  const uint32_t objectPosition = result.graph->RequiresObjectSpacePosition()
      ? uint32_t(ispc::PATH_CONTEXT_OBJECT_POSITION)
      : 0u;
  std::vector<GeomPropBinding> geomProps(names.size());
  for (size_t h = 0; h < names.size(); h++) {
    const size_t separator = names[h].rfind(GEOMPROP_TYPE_SEPARATOR);
    if (separator == std::string::npos)
      continue;
    geomProps[h].attribute = attributeFromName(names[h].substr(0, separator));
    geomProps[h].type = geomPropType(names[h].substr(separator + 1));
  }
  return std::unique_ptr<CppMaterial>(new CppMaterial(std::move(result.graph),
      std::move(geomProps),
      std::move(samplers),
      std::move(images),
      contextGroupsRead(typed) | objectPosition));
}

namespace {

using mxcpp::SurfaceClosure;
using mxcpp::Vec3f;

// Closure arena //////////////////////////////////////////////////////////////

// What the shading call keeps of one lane's material for the calls by handle
// at the same path vertex
struct ArenaEntry
{
  // As evaluated, presence resolved: emission, medium and summary fields
  SurfaceClosure closure;
  // The closure with caustic-class lobes removed, when the lane asked
  SurfaceClosure pruned;
  bool usePruned{false};
  bool hasBsdf{false};
  mxcpp::SurfaceInteraction interaction;
  mxcpp::AdobeOpenPbrPreparedSurface adobe;
  // From the BSDF sample, for the subsurface call
  Vec3f sampleDir{0.f};
  Vec3f lobeNormal{0.f};
  bool sampledEntryDir{false};

  // The BSDF that samples and evaluates (Typhoon's bsdfClosure), or null
  const SurfaceClosure *bsdf() const
  {
    return hasBsdf ? (usePruned ? &pruned : &closure) : nullptr;
  }
};

// The closures of the path vertex the calling thread's gang is at. A handle
// is the slot in the low bits and the arena's generation above, so a handle
// used after the next shading call is refused rather than reading another
// lane's closure (until the generation wraps: this catches misuse, it doesn't
// guarantee against it).
class ClosureArena
{
 public:
  // Starts a new path vertex: the previous one's handles stop working
  void reset()
  {
    m_generation = (m_generation + 1) & GENERATION_MASK;
    m_count = 0;
  }

  // The next free entry, to fill and then keep with commit() or drop by not
  // committing it
  ArenaEntry &next()
  {
    if (m_count == m_entries.size())
      m_entries.emplace_back();
    return m_entries[m_count];
  }

  int commit()
  {
    return int((m_generation << SLOT_BITS) | m_count++);
  }

  const ArenaEntry *get(int handle) const
  {
    const uint32_t h = uint32_t(handle);
    const uint32_t slot = h & (MAX_SLOTS - 1);
    if (handle < 0 || (h >> SLOT_BITS) != m_generation || slot >= m_count)
      return nullptr;
    return &m_entries[slot];
  }

 private:
  // One shading call keeps at most one entry per lane
  static constexpr uint32_t SLOT_BITS = 5;
  static constexpr uint32_t MAX_SLOTS = 1u << SLOT_BITS;
  static constexpr uint32_t GENERATION_MASK = (1u << (31 - SLOT_BITS)) - 1;
  static_assert(MAX_SLOTS >= uint32_t(MAX_LANES));

  // Grow-only: entries keep their closures' storage between generations,
  // and a deque never moves them
  std::deque<ArenaEntry> m_entries;
  uint32_t m_count{0};
  uint32_t m_generation{0};
};

thread_local ClosureArena t_arena;

// Lanes ///////////////////////////////////////////////////////////////////////

int lowestLane(uint32_t m)
{
#ifdef _MSC_VER
  unsigned long i;
  _BitScanForward(&i, m);
  return int(i);
#else
  return __builtin_ctz(m);
#endif
}

template <typename F>
void forEachLane(int mask, F &&f)
{
  for (uint32_t m = uint32_t(mask); m; m &= m - 1)
    f(lowestLane(m));
}

Vec3f vec3(const float *x, const float *y, const float *z, int i)
{
  return Vec3f(x[i], y[i], z[i]);
}

void store(float *x, float *y, float *z, int i, const Vec3f &v)
{
  x[i] = v.x;
  y[i] = v.y;
  z[i] = v.z;
}

Vec3f faced(const Vec3f &n, bool frontFacing)
{
  return frontFacing ? n : -n;
}

Vec3f derivative(const ispc::PathShadeInSoA &in, int row, int i)
{
  const auto &d = in.derivatives[row];
  return Vec3f(d[0][i], d[1][i], d[2][i]);
}

// An affine 3x4 (columns of the linear part, then the translation) as
// Imath's row-vector 4x4: the same 16 floats as a column-major 4x4
mxcpp::Mat4f affine(const float (&m)[12][MAX_LANES], int i)
{
  return mxcpp::Mat4f(m[0][i],
      m[1][i],
      m[2][i],
      0.f,
      m[3][i],
      m[4][i],
      m[5][i],
      0.f,
      m[6][i],
      m[7][i],
      m[8][i],
      0.f,
      m[9][i],
      m[10][i],
      m[11][i],
      1.f);
}

// What the geomprop lookup of lane i reads
struct GeomPropLookup
{
  const CppMaterial *material;
  const ispc::PathShadeInSoA *in;
  int lane;
};

// mxcpp::ShadingContext::GeomPropFn: the lane's attribute for a geomprop
// handle (CppMaterial), as the node's type; empty (the node's default, as
// MaterialX and Typhoon give for a primvar the mesh lacks) for a geomprop that
// isn't an attribute or an attribute the hit doesn't have
// (PathShadeInSoA::attributePresent). halcyon's own geomprop nodes, which its
// material subtypes' attribute parameters and samplers' inAttribute read, have
// the default (0, 0, 0, 1), every renderer's value of a missing attribute.
mxcpp::Value lookupGeomProp(const void *userData, int handle)
{
  const auto &l = *static_cast<const GeomPropLookup *>(userData);
  const std::vector<GeomPropBinding> &bindings = l.material->geomProps();
  if (handle < 0 || size_t(handle) >= bindings.size())
    return {};
  const GeomPropBinding &b = bindings[handle];
  const int i = l.lane;
  if (b.attribute < 0 || !(l.in->attributePresent[i] & (1u << b.attribute)))
    return {};
  const auto &a = l.in->attributes[b.attribute];
  switch (b.type) {
  case GeomPropType::FLOAT:
    return mxcpp::Value(a[0][i]);
  case GeomPropType::VECTOR2:
    return mxcpp::Value(mxcpp::Vec2f(a[0][i], a[1][i]));
  case GeomPropType::VECTOR3:
    return mxcpp::Value(mxcpp::Vec3f(a[0][i], a[1][i], a[2][i]));
  case GeomPropType::VECTOR4:
    return mxcpp::Value(mxcpp::Vec4f(a[0][i], a[1][i], a[2][i], a[3][i]));
  case GeomPropType::INTEGER:
    return mxcpp::Value(int(a[0][i]));
  case GeomPropType::BOOLEAN:
    return mxcpp::Value(a[0][i] != 0.f);
  case GeomPropType::UNSUPPORTED:
  default:
    return {};
  }
}

// Typhoon's mxcpp::ShadingContext of lane i (Renderer::_BuildShadingContext()
// fills it; ShadingContext.isph builds its values) with the renderer's
// services halcyon has: geomprop lookups of 'material' (through 'lookup',
// which must outlive the context), its texture system, the colour transform
// callback and the working space's luminance (ColorManagement.h). Not yet:
// frame and time, and uniform (string) geomprops, which no halcyon attribute
// has. Of the PathContextGroup parts, only those in the lane's contextMask
// (those its graph can read) are filled; the others keep their defaults.
// Without 'screenSpaceDerivatives' the screen-space derivatives stay zero, as
// in Typhoon's visibility-only evaluations.
mxcpp::ShadingContext shadingContext(const ispc::PathMaterialContext &context,
    const ispc::PathShadeInSoA &in,
    int i,
    bool screenSpaceDerivatives,
    const CppMaterial *material,
    GeomPropLookup &lookup)
{
  mxcpp::ShadingContext ctx;
  lookup = {material, &in, i};
  if (material) {
    ctx.geomPropLookup = &lookupGeomProp;
    ctx.geomPropUserData = &lookup;
    ctx.textureSystem = &material->textures();
  }
  ctx.colorTransform = &colorTransform;
  ctx.bypassColorTransforms = false;
  static const mxcpp::Vec3f luminance = luminanceCoefficients();
  ctx.luminanceCoefficients = luminance;
  ctx.position = vec3(in.Pox, in.Poy, in.Poz, i);
  ctx.normal = vec3(in.Nsx, in.Nsy, in.Nsz, i);
  ctx.tangent = vec3(in.Tx, in.Ty, in.Tz, i);
  ctx.bitangent = vec3(in.Bx, in.By, in.Bz, i);
  ctx.viewPosition = Vec3f(context.viewPosition[0],
      context.viewPosition[1],
      context.viewPosition[2]);
  ctx.texcoord = mxcpp::Vec2f(in.u[i], in.v[i]);
  const uint32_t groups = in.contextMask[i];
  if (groups & ispc::PATH_CONTEXT_DISPLAY_COLOR) {
    ctx.displayColor = vec3(in.dcr, in.dcg, in.dcb, i);
    ctx.displayOpacity = in.dco[i];
  }
  if (groups & ispc::PATH_CONTEXT_PRIMITIVE) {
    ctx.faceId = in.faceId[i];
    ctx.baryU = in.baryU[i];
    ctx.baryV = in.baryV[i];
  }
  if (groups & ispc::PATH_CONTEXT_SURFACE_DERIVATIVES) {
    ctx.dPdu = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPDU, i);
    ctx.dPdv = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPDV, i);
    ctx.dPositiondu = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPOSITIONDU, i);
    ctx.dPositiondv = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPOSITIONDV, i);
  }
  if (screenSpaceDerivatives
      && (groups & ispc::PATH_CONTEXT_SCREEN_DERIVATIVES)) {
    ctx.dPdx = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPDX, i);
    ctx.dPdy = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPDY, i);
    ctx.dPositiondx = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPOSITIONDX, i);
    ctx.dPositiondy = derivative(in, ispc::PATH_SOA_DERIVATIVE_DPOSITIONDY, i);
    ctx.dudx = in.dudx[i];
    ctx.dvdx = in.dvdx[i];
    ctx.dudy = in.dudy[i];
    ctx.dvdy = in.dvdy[i];
  }
  if (groups & ispc::PATH_CONTEXT_TRANSFORMS) {
    ctx.objectToWorldMatrix = affine(in.objectToWorld, i);
    ctx.worldToObjectMatrix = affine(in.worldToObject, i);
    ctx.hasObjectToWorldTransform = true;
    ctx.hasWorldToObjectTransform = true;
  }
  return ctx;
}

// Typhoon replaces the material at a subsurface walk's exit with this, so
// that the walk's albedo isn't counted twice (as Cycles does)
SurfaceClosure subsurfaceExitClosure()
{
  SurfaceClosure closure;
  closure.baseColor = Vec3f(1.f);
  closure.roughness = 1.f;
  // The defaults include a dielectric specular lobe; the exit is diffuse only
  closure.specular = 0.f;
  closure.specularColor = Vec3f(0.f);
  closure.opacity = 1.f;
  closure.presence = 1.f;
  return closure;
}

void clearShadeOut(ispc::PathShadeOutSoA &out, int i)
{
  out.handle[i] = -1;
  out.flags[i] = 0;
  out.er[i] = out.eg[i] = out.eb[i] = 0.f;
  out.wix[i] = out.wiy[i] = out.wiz[i] = 0.f;
  out.fr[i] = out.fg[i] = out.fb[i] = 0.f;
  out.pdf[i] = 0.f;
  out.sampleFlags[i] = 0;
  out.eta[i] = 1.f;
  out.heroWavelengthPdf[i] = 0.f;
  out.ar[i] = out.ag[i] = out.ab[i] = 0.f;
}

// Typhoon's path vertex (pathIntegrator.cpp, _IntegratePath()) from graph
// evaluation to the BSDF sample, in its order
void shadeLane(const ispc::PathMaterialContext &context,
    const ispc::PathShadeInSoA &in,
    int i,
    ispc::PathShadeOutSoA &out)
{
  const uint32_t flags = in.flags[i];
  const bool frontFacing = flags & ispc::PATH_SHADE_FRONT_FACING;
  const Vec3f Nsrf = vec3(in.Nsx, in.Nsy, in.Nsz, i);
  const Vec3f Ng = vec3(in.Ngx, in.Ngy, in.Ngz, i);
  const Vec3f wo = vec3(in.wox, in.woy, in.woz, i);

  clearShadeOut(out, i);
  store(out.Nsx, out.Nsy, out.Nsz, i, faced(Nsrf, frontFacing));
  out.heroWavelengthNm[i] = in.heroWavelengthNm[i];

  const bool subsurfaceExit = flags & ispc::PATH_SHADE_SUBSURFACE_EXIT;
  const auto *material = static_cast<const CppMaterial *>(in.material[i]);
  if (!material && !subsurfaceExit)
    return;

  GeomPropLookup lookup;
  const mxcpp::ShadingContext ctx =
      shadingContext(context, in, i, true, material, lookup);
  ArenaEntry &e = t_arena.next();
  if (subsurfaceExit) {
    e.closure = subsurfaceExitClosure();
  } else {
    mxcpp::EvalOptions options;
    options.useAdobeOpenPBR = context.useAdobeOpenPBR;
    e.closure = material->graph().Evaluate(ctx, options);
  }
  SurfaceClosure &closure = e.closure;

  // Normals: resolved in the exterior frame, so front and back faces see one
  // relief, then faced to the incident side and prepared per leaf
  const Vec3f NsExt = mxcpp::ResolveGraphNormal(closure, ctx);
  mxcpp::ValidateLeafNormals(&closure.bsdfTree, NsExt);
  const Vec3f NsOut = faced(NsExt, frontFacing);
  const Vec3f NgOut = faced(Ng, frontFacing);
  mxcpp::Bsdf::detail::PrepareShadingNormals(
      &closure.bsdfTree, NsOut, NgOut, wo);
  store(out.Nsx, out.Nsy, out.Nsz, i, NsOut);

  // Presence is the probability of interacting; a rejected hit is a null
  // event. The strict u < presence keeps u = 0 from hitting an absent surface.
  if (closure.presence < 1.f) {
    const float presence = std::clamp(closure.presence, 0.f, 1.f);
    if (presence <= 0.f || in.uPresence[i] >= presence) {
      out.flags[i] = ispc::PATH_MATERIAL_PRESENCE_SKIPPED;
      return;
    }
    closure.presence = 1.f;
  }

  // Medium-only boundaries have no BSDF; pruning may leave none either
  e.hasBsdf = !isVolumeOnlyBoundary(closure);
  e.usePruned = false;
  if (flags & ispc::PATH_SHADE_PRUNE_CAUSTICS) {
    e.pruned = mxcpp::Bsdf::PruneCausticClassLobes(closure);
    if (closure.HasBsdfTree() && !e.pruned.HasBsdfTree())
      e.hasBsdf = false;
    else
      e.usePruned = true;
  }
  const SurfaceClosure *bsdf = e.bsdf();

  uint32_t materialFlags = ispc::PATH_MATERIAL_HAS_CLOSURE;
  if (bsdf)
    materialFlags |= ispc::PATH_MATERIAL_HAS_BSDF;
  if (isVolumeOnlyBoundary(closure))
    materialFlags |= ispc::PATH_MATERIAL_VOLUME_ONLY_BOUNDARY;
  if (closure.thinWalled)
    materialFlags |= ispc::PATH_MATERIAL_THIN_WALLED;
  if (isReflectionOnlyClosure(closure))
    materialFlags |= ispc::PATH_MATERIAL_REFLECTION_ONLY;
  if (bsdf && isReflectionOnlyClosure(*bsdf))
    materialFlags |= ispc::PATH_MATERIAL_BSDF_REFLECTION_ONLY;
  if (bsdf && hasCoupledThickDielectric(*bsdf))
    materialFlags |= ispc::PATH_MATERIAL_COUPLED_THICK_DIELECTRIC;
  if (closure.hasInteriorMedium)
    materialFlags |= ispc::PATH_MATERIAL_HAS_INTERIOR_MEDIUM;
  if (bsdf && bsdf->HasSubsurfaceScattering())
    materialFlags |= ispc::PATH_MATERIAL_HAS_SUBSURFACE;
  // the albedo channel's summary, of the closure as evaluated (not pruned)
  if (flags & ispc::PATH_SHADE_ALBEDO) {
    const ClosureAlbedo albedo = closureAlbedo(closure);
    store(out.ar, out.ag, out.ab, i, albedo.albedo);
    if (albedo.specularOnly)
      materialFlags |= ispc::PATH_MATERIAL_SPECULAR_ONLY;
  }

  // The first dispersive closure on an RGB path picks the hero wavelength
  float heroWavelengthNm = in.heroWavelengthNm[i];
  if (bsdf && bsdf->HasDispersion() && !(heroWavelengthNm > 0.f)) {
    heroWavelengthNm = mxcpp::Spectral::SampleHeroWavelength(in.uWavelength[i]);
    out.heroWavelengthNm[i] = heroWavelengthNm;
    out.heroWavelengthPdf[i] = mxcpp::Spectral::HeroWavelengthPdf();
    materialFlags |= ispc::PATH_MATERIAL_HERO_STARTED;
  }

  mxcpp::SurfaceInteraction &si = e.interaction;
  si.normalShdWldOut = NsOut;
  si.normalSrfWldOut = faced(Nsrf, frontFacing);
  si.normalGeomWldOut = NgOut;
  si.omegaOutWld = wo;
  si.heroWavelengthNm = heroWavelengthNm;
  si.frontFacing = frontFacing;

  e.adobe = bsdf ? mxcpp::PrepareAdobeOpenPbrSurface(*bsdf, si)
                 : mxcpp::AdobeOpenPbrPreparedSurface{};

  e.sampleDir = Vec3f(0.f);
  e.lobeNormal = Vec3f(0.f);
  e.sampledEntryDir = false;
  if (bsdf) {
    const float u1 = in.u1[i], u2 = in.u2[i], uLobe = in.uLobe[i];
    const mxcpp::Bsdf::BsdfSample s = e.adobe.valid
        ? mxcpp::SamplePreparedAdobeOpenPbrSurface(e.adobe, u1, u2, uLobe)
        : sampleSurface(*bsdf, si, u1, u2, uLobe);
    store(out.wix, out.wiy, out.wiz, i, s.omegaInWld);
    store(out.fr,
        out.fg,
        out.fb,
        i,
        s.isSpecular || s.isSubsurface ? s.bsdfValue : s.bsdfValueCosine);
    out.pdf[i] = s.pdfSolidAngle;
    out.eta[i] = s.eta;
    out.sampleFlags[i] =
        (s.isSubsurface || s.pdfSolidAngle > 0.f ? ispc::PATH_SAMPLE_VALID : 0)
        | (s.isSpecular ? ispc::PATH_SAMPLE_SPECULAR : 0)
        | (s.isTransmission ? ispc::PATH_SAMPLE_TRANSMISSION : 0)
        | (s.isSubsurface ? ispc::PATH_SAMPLE_SUBSURFACE : 0)
        | (s.isDiffuseLike ? ispc::PATH_SAMPLE_DIFFUSE_LIKE : 0);
    e.sampleDir = s.omegaInWld;
    e.lobeNormal = s.normalShdLobeWldOut;
    e.sampledEntryDir = s.hasSubsurfaceEntryDirection;
  }

  store(out.er, out.eg, out.eb, i, closure.emissiveColor);
  out.flags[i] = materialFlags;
  out.handle[i] = t_arena.commit();
}

void storeMedium(
    ispc::PathMediumSoA &out, int i, const mxcpp::MediumProperties &m)
{
  store(out.ar, out.ag, out.ab, i, m.absorption);
  store(out.sr, out.sg, out.sb, i, m.scattering);
  out.anisotropy[i] = m.anisotropy;
  out.transportModel[i] = int(m.transportModel);
  store(
      out.aer, out.aeg, out.aeb, i, m.adobeOpenPbrVolume.extinctionCoefficient);
  store(out.aar, out.aag, out.aab, i, m.adobeOpenPbrVolume.albedo);
  out.adobeAnisotropy[i] = m.adobeOpenPbrVolume.anisotropy;
  out.adobeValid[i] = m.adobeOpenPbrVolume.valid;
}

} // namespace

} // namespace halcyon::pathtracer

// The callouts //////////////////////////////////////////////////////////////

using namespace halcyon::pathtracer;

extern "C" void PathMaterial_shadeSoA(const ispc::PathMaterialContext *context,
    int mask,
    const ispc::PathShadeInSoA *in,
    ispc::PathShadeOutSoA *out)
{
  // A shading call starts the gang's next path vertex: the closures of its
  // previous one are no longer needed
  t_arena.reset();
  forEachLane(mask, [&](int i) { shadeLane(*context, *in, i, *out); });
}

extern "C" void PathMaterial_evalSoA(
    int mask, const ispc::PathEvalInSoA *in, ispc::PathEvalOutSoA *out)
{
  forEachLane(mask, [&](int i) {
    Vec3f f(0.f);
    float pdf = 0.f;
    const ArenaEntry *e = t_arena.get(in->handle[i]);
    if (const SurfaceClosure *bsdf = e ? e->bsdf() : nullptr) {
      // As Typhoon's NEE (_ComputeDirectLightingMIS())
      const Vec3f wi = vec3(in->wix, in->wiy, in->wiz, i);
      const mxcpp::AdobeOpenPbrEvalPdfResult adobe = e->adobe.valid
          ? mxcpp::EvalPdfPreparedAdobeOpenPbrSurface(e->adobe, wi)
          : mxcpp::TryEvalPdfAdobeOpenPbrSurface(*bsdf, e->interaction, wi);
      if (adobe.evaluated) {
        f = adobe.valueCosine;
        pdf = adobe.pdfSolidAngle;
      } else {
        f = mxcpp::Bsdf::EvalSurfaceCosine(*bsdf, e->interaction, wi);
        pdf = mxcpp::Bsdf::PdfSurface(*bsdf, e->interaction, wi);
      }
      for (int k = 0; k < 3; k++) {
        if (!std::isfinite(f[k]))
          f[k] = 0.f;
      }
    }
    store(out->fr, out->fg, out->fb, i, f);
    out->pdf[i] = pdf;
  });
}

extern "C" void PathMaterial_subsurfaceSoA(int mask,
    const ispc::PathSubsurfaceInSoA *in,
    ispc::PathSubsurfaceOutSoA *out)
{
  forEachLane(mask, [&](int i) {
    out->valid[i] = 0;
    store(out->dx, out->dy, out->dz, i, Vec3f(0.f));
    store(out->ar, out->ag, out->ab, i, Vec3f(0.f));
    store(out->rr, out->rg, out->rb, i, Vec3f(0.f));
    out->anisotropy[i] = 0.f;
    out->usePrecomputed[i] = 0;
    store(out->sar, out->sag, out->sab, i, Vec3f(0.f));
    store(out->ssr, out->ssg, out->ssb, i, Vec3f(0.f));

    const ArenaEntry *e = t_arena.get(in->handle[i]);
    const SurfaceClosure *bsdf = e ? e->bsdf() : nullptr;
    if (!bsdf)
      return;

    // As Typhoon's _TraceSubsurface(): the sample's direction, else one from
    // the surface's dielectric; it must point below both the lobe's and the
    // geometric normal
    const mxcpp::SurfaceInteraction &si = e->interaction;
    Vec3f dir = e->sampleDir;
    bool valid = true;
    if (!e->sampledEntryDir) {
      valid = mxcpp::Bsdf::SampleSubsurfaceEntry(*bsdf,
          e->lobeNormal,
          si.normalGeomWldOut,
          si.omegaOutWld,
          in->u1[i],
          in->u2[i],
          dir);
    }
    valid = valid && e->lobeNormal.dot(dir) < 0.f
        && si.normalGeomWldOut.dot(dir) < 0.f;
    out->valid[i] = valid;
    if (valid)
      store(out->dx, out->dy, out->dz, i, dir);

    store(out->ar, out->ag, out->ab, i, bsdf->subsurfaceColor);
    store(out->rr,
        out->rg,
        out->rb,
        i,
        bsdf->subsurfaceRadius * bsdf->subsurfaceRadiusScale);
    out->anisotropy[i] = std::clamp(bsdf->subsurfaceAnisotropy, -0.99f, 0.99f);
    const mxcpp::MediumProperties &pre = bsdf->precomputedSubsurfaceMedium;
    if (bsdf->hasPrecomputedSubsurfaceMedium && !pre.IsVacuum()) {
      out->usePrecomputed[i] = 1;
      store(out->sar, out->sag, out->sab, i, pre.absorption);
      store(out->ssr, out->ssg, out->ssb, i, pre.scattering);
      out->anisotropy[i] = std::clamp(pre.anisotropy, -0.99f, 0.99f);
    }
  });
}

extern "C" void PathMaterial_mediumSoA(
    int mask, const ispc::PathHandleSoA *in, ispc::PathMediumSoA *out)
{
  forEachLane(mask, [&](int i) {
    const ArenaEntry *e = t_arena.get(in->handle[i]);
    storeMedium(*out,
        i,
        e && e->closure.hasInteriorMedium ? e->closure.interiorMedium
                                          : mxcpp::MediumProperties{});
  });
}

extern "C" void PathMaterial_shadowSoA(const ispc::PathMaterialContext *context,
    int mask,
    const ispc::PathShadeInSoA *in,
    ispc::PathShadowOutSoA *out)
{
  forEachLane(mask, [&](int i) {
    out->flags[i] = 0;
    out->opacity[i] = 0.f;
    out->presence[i] = 0.f;
    out->transmission[i] = 0.f;
    out->straightTransmission[i] = 0.f;
    store(out->tr, out->tg, out->tb, i, Vec3f(0.f));
    storeMedium(out->medium, i, mxcpp::MediumProperties{});

    const auto *material = static_cast<const CppMaterial *>(in->material[i]);
    if (!material)
      return;

    // As Typhoon's _TryEvalSurfaceClosureAtHit(): a visibility-only
    // evaluation, without normal preparation
    mxcpp::EvalOptions options;
    options.useAdobeOpenPBR = context->useAdobeOpenPBR;
    options.visibilityOnly = true;
    GeomPropLookup lookup;
    const SurfaceClosure closure = material->graph().Evaluate(
        shadingContext(*context, *in, i, false, material, lookup), options);

    uint32_t flags = ispc::PATH_MATERIAL_HAS_CLOSURE;
    if (isVolumeOnlyBoundary(closure))
      flags |= ispc::PATH_MATERIAL_VOLUME_ONLY_BOUNDARY;
    if (closure.thinWalled)
      flags |= ispc::PATH_MATERIAL_THIN_WALLED;
    if (closure.hasInteriorMedium)
      flags |= ispc::PATH_MATERIAL_HAS_INTERIOR_MEDIUM;
    // Typhoon's _Visibility() enters a volume-only boundary's medium whatever
    // hasInteriorMedium says
    if (closure.hasInteriorMedium || isVolumeOnlyBoundary(closure))
      storeMedium(out->medium, i, closure.interiorMedium);
    out->flags[i] = flags;
    out->opacity[i] = closure.opacity;
    out->presence[i] = closure.presence;
    out->transmission[i] = closure.transmission;
    store(out->tr, out->tg, out->tb, i, closure.transmissionColor);

    // Typhoon's _TransparentShadowTransmission() without the tint; the shadow
    // ray runs against wo
    const float transmission = std::clamp(closure.transmission, 0.f, 1.f);
    if (transmission > 0.f) {
      const Vec3f Ng = vec3(in->Ngx, in->Ngy, in->Ngz, i);
      const Vec3f wo = vec3(in->wox, in->woy, in->woz, i);
      out->straightTransmission[i] = std::clamp(transmission
              * mxcpp::Bsdf::StraightShadowDielectricTransmission(
                  closure, -wo.dot(Ng)),
          0.f,
          1.f);
    }
  });
}
