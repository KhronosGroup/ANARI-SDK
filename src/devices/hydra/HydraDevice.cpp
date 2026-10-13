// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HydraDevice.h"

#include "DelegateRegistry.h"
#include "Introspection.h"
#include "camera/Camera.h"
#include "frame/Frame.h"
#include "geometry/Geometry.h"
#include "hydra/Worker.h"
#include "light/Light.h"
// pxr
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/types.h>
#include <pxr/imaging/hf/pluginDesc.h>
#include <pxr/usd/sdr/registry.h>
#include "material/Material.h"
#include "renderer/Renderer.h"
#include "sampler/Sampler.h"
#include "scene/AnariImage.h"
#include "scene/VolumeBuilder.h"
#include "spatial_field/SpatialField.h"
#include "surface/Surface.h"
#include "volume/Volume.h"
#include "world/Group.h"
#include "world/Instance.h"
#include "world/World.h"
// helium
#include "helium/array/Array1D.h"
#include "helium/array/Array2D.h"
#include "helium/array/Array3D.h"
#include "helium/array/ObjectArray.h"
// generated from introspection/*.json (anari_generate_queries)
#include "anari_library_hydra_queries.h"
// std
#include <algorithm>
#include <cstring>
#include <map>
#include <string>

namespace anari_hydra {

PXR_NAMESPACE_USING_DIRECTIVE

// Per-delegate extension truth (SPEC §4.8) //////////////////////////////////

static bool hasType(const TfTokenVector &v, const TfToken &t)
{
  return std::find(v.begin(), v.end(), t) != v.end();
}

// Volumes need the volume rprim, the field bprim type the bridge authors, and
// the material sprim carrying the volume shader (SPEC §7). Storm
// has all three; HdEmbree has none.
static bool supportsVolumes(const TfTokenVector &rprims,
    const TfTokenVector &sprims,
    const TfTokenVector &bprims)
{
  return hasType(rprims, HdPrimTypeTokens->volume)
      && hasType(sprims, HdPrimTypeTokens->material)
      && hasType(bprims, FieldPrimType());
}

// VtValue -> ANARIDataType for render-setting reflection (SPEC §11.3). Covers
// the common scalar/vector holdings; anything else returns ANARI_UNKNOWN (the
// setting is dropped from reflection, warned once at probe time). double folds
// to FLOAT32 (ANARI has no f64 scalar); string/token both surface as STRING.
static ANARIDataType vtValueToAnariType(const VtValue &v)
{
  if (v.IsHolding<bool>())
    return ANARI_BOOL;
  if (v.IsHolding<int>())
    return ANARI_INT32;
  if (v.IsHolding<unsigned int>())
    return ANARI_UINT32;
  if (v.IsHolding<float>())
    return ANARI_FLOAT32;
  if (v.IsHolding<double>())
    return ANARI_FLOAT32;
  if (v.IsHolding<std::string>() || v.IsHolding<TfToken>())
    return ANARI_STRING;
  if (v.IsHolding<GfVec2f>())
    return ANARI_FLOAT32_VEC2;
  if (v.IsHolding<GfVec3f>())
    return ANARI_FLOAT32_VEC3;
  if (v.IsHolding<GfVec4f>())
    return ANARI_FLOAT32_VEC4;
  return ANARI_UNKNOWN;
}

// Serialize a render setting's default VtValue into ANARI-typed bytes (for
// getParameterInfo "default"); string defaults go to `defaultString`. Best-
// effort: leaves the buffer empty for holdings we don't convert.
static void serializeVtDefault(const VtValue &v, RenderSettingDesc &d)
{
  auto put = [&](const void *src, size_t n) {
    d.defaultBytes.resize(n);
    std::memcpy(d.defaultBytes.data(), src, n);
  };
  switch (d.type) {
  case ANARI_BOOL: {
    int8_t b = v.IsHolding<bool>() ? (v.UncheckedGet<bool>() ? 1 : 0) : 0;
    put(&b, sizeof(b));
    break;
  }
  case ANARI_INT32: {
    int32_t i = v.IsHolding<int>() ? v.UncheckedGet<int>() : 0;
    put(&i, sizeof(i));
    break;
  }
  case ANARI_UINT32: {
    uint32_t u =
        v.IsHolding<unsigned int>() ? v.UncheckedGet<unsigned int>() : 0u;
    put(&u, sizeof(u));
    break;
  }
  case ANARI_FLOAT32: {
    float f = v.IsHolding<float>()
        ? v.UncheckedGet<float>()
        : (v.IsHolding<double>() ? float(v.UncheckedGet<double>()) : 0.f);
    put(&f, sizeof(f));
    break;
  }
  case ANARI_FLOAT32_VEC2:
    if (v.IsHolding<GfVec2f>())
      put(v.UncheckedGet<GfVec2f>().data(), 2 * sizeof(float));
    break;
  case ANARI_FLOAT32_VEC3:
    if (v.IsHolding<GfVec3f>())
      put(v.UncheckedGet<GfVec3f>().data(), 3 * sizeof(float));
    break;
  case ANARI_FLOAT32_VEC4:
    if (v.IsHolding<GfVec4f>())
      put(v.UncheckedGet<GfVec4f>().data(), 4 * sizeof(float));
    break;
  case ANARI_STRING:
    if (v.IsHolding<std::string>())
      d.defaultString = v.UncheckedGet<std::string>();
    else if (v.IsHolding<TfToken>())
      d.defaultString = v.UncheckedGet<TfToken>().GetString();
    break;
  default:
    break;
  }
}

const std::vector<AovChannelDesc> &standardAovChannels()
{
  // Depth/normal use FLOAT32/FLOAT32_VEC3; normal is world space, backed by
  // the delegate's `normal` AOV or (Storm) its eye-space `Neye` AOV. Standard
  // ID channels translate Hydra indices to ANARI user IDs (SPEC §11.4).
  static const std::vector<AovChannelDesc> table = {
      {"channel.depth",
          HdAovTokens->depth,
          ANARI_FLOAT32,
          "ANARI_KHR_FRAME_CHANNEL_DEPTH"},
      {"channel.normal",
          HdAovTokens->normal,
          ANARI_FLOAT32_VEC3,
          "ANARI_KHR_FRAME_CHANNEL_NORMAL"},
      {"channel.objectId",
          HdAovTokens->primId,
          ANARI_UINT32,
          "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID"},
      {"channel.primitiveId",
          HdAovTokens->elementId,
          ANARI_UINT32,
          "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID"},
      {"channel.instanceId",
          HdAovTokens->instanceId,
          ANARI_UINT32,
          "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID"},
  };
  return table;
}

TfToken normalAovToken(HdRenderDelegate *delegate)
{
  auto hasAov = [&](const TfToken &tok) {
    return delegate->GetDefaultAovDescriptor(tok).format != HdFormatInvalid;
  };
  if (hasAov(HdAovTokens->normal))
    return HdAovTokens->normal;
  if (hasAov(HdAovTokens->Neye))
    return HdAovTokens->Neye;
  return TfToken();
}

void readDelegateCaps(const TfToken &delegateId,
    TfTokenVector &rprims,
    TfTokenVector &sprims,
    TfTokenVector &bprims,
    std::set<std::string> &aovChannels,
    std::vector<RenderSettingDesc> &renderSettings)
{
  // A bare delegate instance suffices to read the supported-type tokens; the
  // render index / Hgi driver (needed for actual rendering) is not. The handle
  // frees the delegate as it goes out of scope.
  HdPluginRenderDelegateUniqueHandle delegate =
      HdRendererPluginRegistry::GetInstance().CreateRenderDelegate(delegateId);
  if (!delegate)
    return;
  rprims = delegate->GetSupportedRprimTypes();
  sprims = delegate->GetSupportedSprimTypes();
  bprims = delegate->GetSupportedBprimTypes();

  // A standard AOV channel is offered iff the delegate returns a valid default
  // descriptor for its Hydra token (SPEC §11.4/§4.8); normal resolves through
  // normalAovToken (its `normal` AOV, or Storm's eye-space `Neye`). This is a
  // descriptor probe: that the bound pass actually fills the target is proven
  // by the render tests (render_normal_channel), not at probe time.
  aovChannels.clear();
  auto hasAov = [&](const TfToken &tok) {
    return delegate->GetDefaultAovDescriptor(tok).format != HdFormatInvalid;
  };
  for (const AovChannelDesc &d : standardAovChannels()) {
    // Primitive and instance translation also need the owning rprim ID.
    if ((d.hydraToken == HdAovTokens->elementId
            || d.hydraToken == HdAovTokens->instanceId)
        && !hasAov(HdAovTokens->primId))
      continue;
    const bool offered = d.hydraToken == HdAovTokens->normal
        ? !normalAovToken(delegate.Get()).IsEmpty()
        : hasAov(d.hydraToken);
    if (offered)
      aovChannels.insert(d.anariChannel);
  }

  // Every HdRenderSettingDescriptor the delegate publishes becomes a reflected,
  // typed ANARI renderer parameter under its verbatim Hydra token (SPEC §11.3).
  // Captured with the ANARIDataType its default VtValue maps to (ANARI_UNKNOWN
  // if unmapped; those are dropped + warned when the reflection list is built).
  renderSettings.clear();
  for (const HdRenderSettingDescriptor &d :
      delegate->GetRenderSettingDescriptors()) {
    RenderSettingDesc rs;
    rs.token = d.key.GetString();
    rs.humanName = d.name;
    rs.type = vtValueToAnariType(d.defaultValue);
    if (rs.type != ANARI_UNKNOWN)
      serializeVtDefault(d.defaultValue, rs);
    renderSettings.push_back(std::move(rs));
  }
}

void buildExtensionList(const TfTokenVector &rprims,
    const TfTokenVector &sprims,
    const TfTokenVector &bprims,
    const std::set<std::string> &aovChannels,
    std::vector<std::string> &outStrings,
    std::vector<const char *> &outView)
{
  std::vector<std::string> &e = outStrings;
  e.clear();

  // ALWAYS present: every real delegate publishes `mesh`, so the mesh-backed
  // geometry (triangle/quad and the tessellated cone/cylinder), the
  // matte/physicallyBased materials (which degrade to displayColor per §6.9 on
  // delegates that ignore material networks), the perspective camera, the color
  // channel, and the render-settings extension are unconditional.
  e.push_back("ANARI_KHR_DEVICE_SYNCHRONIZATION");
  e.push_back("ANARI_KHR_GEOMETRY_TRIANGLE");
  e.push_back("ANARI_KHR_GEOMETRY_QUAD");
  e.push_back("ANARI_KHR_GEOMETRY_CONE");
  e.push_back("ANARI_KHR_GEOMETRY_CYLINDER");
  // Spheres are tessellated meshes; curves require basisCurves.
  if (hasType(rprims, HdPrimTypeTokens->mesh))
    e.push_back("ANARI_KHR_GEOMETRY_SPHERE");
  if (hasType(rprims, HdPrimTypeTokens->basisCurves))
    e.push_back("ANARI_KHR_GEOMETRY_CURVE");
  e.push_back("ANARI_KHR_MATERIAL_MATTE");
  e.push_back("ANARI_KHR_MATERIAL_PHYSICALLY_BASED");
  // Samplers author into the real material-network sprim; HdEmbree has no
  // `material` sprim, so drop the sampler extensions there (§6.4).
  if (hasType(sprims, HdPrimTypeTokens->material)) {
    e.push_back("ANARI_KHR_SAMPLER_IMAGE1D");
    e.push_back("ANARI_KHR_SAMPLER_IMAGE2D");
    e.push_back("ANARI_KHR_SAMPLER_PRIMITIVE");
    e.push_back("ANARI_KHR_SAMPLER_TRANSFORM");
    // clampToBorder with an arbitrary borderColor on image1D/image2D: Storm's
    // transparent-black border wrap, or a border-padded registry image (§6.4).
    e.push_back("ANARI_KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER");
  }
  // Each light extension tracks the UsdLux sprim it maps to (§6.6).
  if (hasType(sprims, HdPrimTypeTokens->distantLight))
    e.push_back("ANARI_KHR_LIGHT_DIRECTIONAL");
  if (hasType(sprims, HdPrimTypeTokens->sphereLight)) {
    e.push_back("ANARI_KHR_LIGHT_POINT");
    e.push_back("ANARI_KHR_LIGHT_SPOT");
  }
  if (hasType(sprims, HdPrimTypeTokens->rectLight))
    e.push_back("ANARI_KHR_LIGHT_QUAD");
  if (hasType(sprims, HdPrimTypeTokens->diskLight))
    e.push_back("ANARI_KHR_LIGHT_RING");
  // hdri and the `default` renderer's ambient fill are both dome lights whose
  // texture carries the radiance (scale/color folded in): Storm and HdEmbree
  // read it through Hio, hdGatling through Ar (§6.3/§6.6).
  if (hasType(sprims, HdPrimTypeTokens->domeLight)) {
    e.push_back("ANARI_KHR_LIGHT_HDRI");
    e.push_back("ANARI_KHR_RENDERER_AMBIENT_LIGHT");
  }
  // structuredRegular (and, when built with NanoVDB, nanovdb) fields
  // classified by transferFunction1D volumes (§7).
  if (supportsVolumes(rprims, sprims, bprims)) {
    e.push_back("ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR");
    if (SpatialField::supportsSubtype("nanovdb"))
      e.push_back("ANARI_KHR_SPATIAL_FIELD_NANOVDB");
    e.push_back("ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D");
    // Isosurfaces extract a mesh from the same field snapshots; offered
    // wherever fields are (README.md "Isosurfaces").
    e.push_back("ANARI_KHR_GEOMETRY_ISOSURFACE");
  }
  e.push_back("ANARI_KHR_CAMERA_PERSPECTIVE");
  e.push_back("ANARI_KHR_CAMERA_ORTHOGRAPHIC");
  // The `default` renderer's background is the frame's clear colour on every
  // delegate (§4.7).
  e.push_back("ANARI_KHR_RENDERER_BACKGROUND_COLOR");
  // A background image is composited under the color AOV on readback (the
  // AOV clears to transparent), so it too is delegate-independent (§4.7).
  e.push_back("ANARI_KHR_RENDERER_BACKGROUND_IMAGE");
  // Instancing rides the core HdInstancer every delegate consumes (§4.3);
  // array transforms flatten onto the same group instancer.
  e.push_back("ANARI_KHR_INSTANCE_TRANSFORM");
  e.push_back("ANARI_KHR_INSTANCE_TRANSFORM_ARRAY");
  e.push_back("ANARI_KHR_FRAME_CHANNEL_COLOR");
  // Completion callbacks fire from the device worker after every render job,
  // independent of the delegate (SPEC §8.7).
  e.push_back("ANARI_KHR_FRAME_COMPLETION_CALLBACK");
  // Standard AOV channels claim their KHR_FRAME_CHANNEL_* extension iff the
  // delegate offers the backing Hydra AOV descriptor (SPEC §11.4).
  for (const AovChannelDesc &d : standardAovChannels()) {
    if (aovChannels.count(d.anariChannel))
      e.push_back(d.extension);
  }
  // Arbitrary named Hydra AOVs surfaced as `channel.aov:<hydraToken>` frame
  // channels (SPEC §11.4). The color channel already proves the AOV path, and
  // every real delegate that renders an AOV publishes the `renderBuffer` bprim
  // (storm and embree both do); gate on it so the claim stays honest per
  // delegate (SPEC §4.8) — a delegate with no render buffer cannot serve one.
  if (hasType(bprims, HdPrimTypeTokens->renderBuffer))
    e.push_back("ANARI_HYDRA_FRAME_AOV");
  e.push_back("ANARI_HYDRA_RENDER_SETTINGS");

  // Build the NUL-terminated C-API view (pointers into `e`, stable after this).
  outView.clear();
  outView.reserve(e.size() + 1);
  for (const std::string &s : e)
    outView.push_back(s.c_str());
  outView.push_back(nullptr);
}

// Data Arrays ////////////////////////////////////////////////////////////////

ANARIArray1D HydraDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  initDevice();

  helium::Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new helium::ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new helium::Array1D(deviceState(), md);
}

ANARIArray2D HydraDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  initDevice();

  helium::Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return (ANARIArray2D) new helium::Array2D(deviceState(), md);
}

ANARIArray3D HydraDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  initDevice();

  helium::Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return (ANARIArray3D) new helium::Array3D(deviceState(), md);
}

// API Objects ////////////////////////////////////////////////////////////////

ANARICamera HydraDevice::newCamera(const char *subtype)
{
  initDevice();
  return (ANARICamera)Camera::createInstance(subtype, deviceState());
}

ANARIFrame HydraDevice::newFrame()
{
  initDevice();
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry HydraDevice::newGeometry(const char *subtype)
{
  initDevice();
  return (ANARIGeometry)Geometry::createInstance(subtype, deviceState());
}

ANARIGroup HydraDevice::newGroup()
{
  initDevice();
  return (ANARIGroup) new Group(deviceState());
}

ANARIInstance HydraDevice::newInstance(const char * /*subtype*/)
{
  initDevice();
  return (ANARIInstance) new Instance(deviceState());
}

ANARILight HydraDevice::newLight(const char *subtype)
{
  initDevice();
  return (ANARILight)Light::createInstance(subtype, deviceState());
}

ANARIMaterial HydraDevice::newMaterial(const char *subtype)
{
  initDevice();
  return (ANARIMaterial)Material::createInstance(subtype, deviceState());
}

ANARIRenderer HydraDevice::newRenderer(const char *subtype)
{
  initDevice();
  return (ANARIRenderer)Renderer::createInstance(subtype, deviceState());
}

ANARISampler HydraDevice::newSampler(const char *subtype)
{
  initDevice();
  if (Sampler *s = Sampler::createInstance(subtype, deviceState()))
    return (ANARISampler)s;
  reportMessage(ANARI_SEVERITY_WARNING,
      "unsupported sampler subtype '%s'; image3D is a documented exclusion "
      "(SPEC §6.4)",
      subtype ? subtype : "");
  return (ANARISampler) new UnknownObject(ANARI_SAMPLER, deviceState());
}

ANARISpatialField HydraDevice::newSpatialField(const char *subtype)
{
  initDevice();
  if (SpatialField *f =
          SpatialField::createInstance(subtype ? subtype : "", deviceState()))
    return (ANARISpatialField)f;
  reportMessage(ANARI_SEVERITY_WARNING,
      "unsupported spatial field subtype '%s' (SPEC §7)",
      subtype ? subtype : "");
  return (ANARISpatialField) new UnknownObject(
      ANARI_SPATIAL_FIELD, deviceState());
}

ANARISurface HydraDevice::newSurface()
{
  initDevice();
  return (ANARISurface) new Surface(deviceState());
}

ANARIVolume HydraDevice::newVolume(const char *subtype)
{
  initDevice();
  if (Volume *v = Volume::createInstance(subtype ? subtype : "", deviceState()))
    return (ANARIVolume)v;
  reportMessage(ANARI_SEVERITY_WARNING,
      "unsupported volume subtype '%s' (SPEC §7)",
      subtype ? subtype : "");
  return (ANARIVolume) new UnknownObject(ANARI_VOLUME, deviceState());
}

ANARIWorld HydraDevice::newWorld()
{
  initDevice();
  return (ANARIWorld) new World(deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

int HydraDevice::getProperty(ANARIObject object,
    const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  if (!handleIsDevice(object) && std::string_view(name) == "bounds"
      && mask == ANARI_WAIT) {
    int result = 0;
    // Helium flushes before dispatch. Move that flush onto the worker too,
    // so a bounds observation cannot finalize geometry during Hydra Sync.
    deviceState()->worker->postWait([&]() {
      result =
          helium::BaseDevice::getProperty(object, name, type, mem, size, mask);
    });
    return result;
  }
  return helium::BaseDevice::getProperty(object, name, type, mem, size, mask);
}

const char **HydraDevice::getObjectSubtypes(ANARIDataType objectType)
{
  // Subtype queries can arrive before any object is created; ensure the worker
  // exists and the delegate capabilities are probed (SPEC §4.8).
  initDevice();
  auto &state = *deviceState();
  const TfTokenVector &rp = state.supportedRprims;
  const TfTokenVector &sp = state.supportedSprims;

  static const char *materials[] = {"matte", "physicallyBased", nullptr};
  static const char *cameras[] = {"perspective", "orthographic", nullptr};
  static const char *renderers[] = {"default", nullptr};
  static const char *instances[] = {"transform", nullptr};
  static const char *fields[] = {"structuredRegular",
      SpatialField::supportsSubtype("nanovdb") ? "nanovdb" : nullptr,
      nullptr};
  static const char *volumes[] = {"transferFunction1D", nullptr};
  static const char *none[] = {nullptr};
  const bool volumeCapable = supportsVolumes(rp, sp, state.supportedBprims);

  switch (objectType) {
  case ANARI_GEOMETRY: {
    // Drop sphere/curve when the delegate lacks their backing rprim.
    auto &g = state.geometrySubtypeView;
    if (g.empty()) {
      g.push_back("triangle");
      g.push_back("quad");
      if (hasType(rp, HdPrimTypeTokens->mesh))
        g.push_back("sphere");
      if (hasType(rp, HdPrimTypeTokens->basisCurves))
        g.push_back("curve");
      g.push_back("cone");
      g.push_back("cylinder");
      // Isosurfaces need a spatial field, offered with volumes (§7).
      if (volumeCapable)
        g.push_back("isosurface");
      g.push_back(nullptr);
    }
    return g.data();
  }
  case ANARI_MATERIAL:
    return materials;
  case ANARI_SAMPLER: {
    // Samplers require the material sprim; empty list where it is absent
    // (§6.4).
    auto &s = state.samplerSubtypeView;
    if (s.empty()) {
      if (hasType(sp, HdPrimTypeTokens->material)) {
        s.push_back("image1D");
        s.push_back("image2D");
        s.push_back("primitive");
        s.push_back("transform");
      }
      s.push_back(nullptr);
    }
    return s.data();
  }
  case ANARI_LIGHT: {
    // Each subtype follows its UsdLux sprim (§6.6).
    auto &l = state.lightSubtypeView;
    if (l.empty()) {
      if (hasType(sp, HdPrimTypeTokens->distantLight))
        l.push_back("directional");
      if (hasType(sp, HdPrimTypeTokens->sphereLight)) {
        l.push_back("point");
        l.push_back("spot");
      }
      if (hasType(sp, HdPrimTypeTokens->rectLight))
        l.push_back("quad");
      if (hasType(sp, HdPrimTypeTokens->diskLight))
        l.push_back("ring");
      if (hasType(sp, HdPrimTypeTokens->domeLight))
        l.push_back("hdri");
      l.push_back(nullptr);
    }
    return l.data();
  }
  case ANARI_CAMERA:
    return cameras;
  case ANARI_RENDERER:
    return renderers;
  case ANARI_INSTANCE:
    return instances;
  // Volume subtypes follow the delegate's volume capability (§7).
  case ANARI_SPATIAL_FIELD:
    return volumeCapable ? fields : none;
  case ANARI_VOLUME:
    return volumeCapable ? volumes : none;
  default:
    return nullptr;
  }
}

// Resolve the subtype an introspection query addresses: nullptr for anonymous
// object types (frame, surface, ...; the argument is ignored), the single
// "default" renderer when none is named, else the caller's string iff it is one
// of the delegate-gated subtypes. Returns false when nothing answers.
bool HydraDevice::resolveQuerySubtype(
    ANARIDataType objectType, const char *&subtype)
{
  const char **list = getObjectSubtypes(objectType);
  if (!list) {
    // Anonymous types the device implements. The SDK's KHR tables also
    // describe objects the bridge has no counterpart for; those answer
    // nothing (as does the anonymous volume entry KHR_FRAME_CHANNEL_OBJECT_ID
    // adds, below: volumes are subtyped and take no ID, README.md).
    switch (objectType) {
    case ANARI_DEVICE:
    case ANARI_ARRAY1D:
    case ANARI_ARRAY2D:
    case ANARI_ARRAY3D:
    case ANARI_FRAME:
    case ANARI_GROUP:
    case ANARI_WORLD:
    case ANARI_SURFACE:
      subtype = nullptr;
      return true;
    default:
      return false;
    }
  }
  if (objectType == ANARI_RENDERER && !subtype)
    subtype = "default";
  if (!subtype)
    return false;
  for (; *list; ++list)
    if (std::strcmp(*list, subtype) == 0)
      return true;
  return false;
}

// The bound delegate's advertised extension list, which the introspection
// filter gates the generated tables on (Introspection.h, SPEC §4.8).
static const std::vector<const char *> &advertisedExtensions(
    HydraGlobalState &state)
{
  if (state.extensionView.empty()) {
    buildExtensionList(state.supportedRprims,
        state.supportedSprims,
        state.supportedBprims,
        state.supportedAovChannels,
        state.extensionStrings,
        state.extensionView);
  }
  return state.extensionView;
}

// Build (once) the reflected renderer parameter list (SPEC §11.3): the bridge's
// own renderer parameters (§4.7, from the generated table, gated per delegate
// like everything else) followed by every delegate render-setting token
// verbatim, skipping tokens a core alias covers (translation wins collisions)
// and any unmapped-type settings (warned). NUL-terminated with a
// {nullptr, UNKNOWN} sentinel; names point at stable storage (the generated
// table / renderSettingDescs).
void HydraDevice::buildRendererParamList()
{
  auto &state = *deviceState();
  auto &view = state.rendererParamView;
  view.clear();

  std::set<std::string> emitted;
  const ANARIParameter *core = reflectedParameterList(
      state, advertisedExtensions(state), ANARI_RENDERER, "default");
  for (; core && core->name; ++core) {
    view.push_back(*core);
    emitted.insert(core->name);
  }

  for (const RenderSettingDesc &d : state.renderSettingDescs) {
    if (d.type == ANARI_UNKNOWN) {
      reportMessage(ANARI_SEVERITY_DEBUG,
          "delegate render setting '%s' has an unmapped default type; not "
          "reflected as an ANARI parameter (SPEC §11.3)",
          d.token.c_str());
      continue;
    }
    // Translation-table alias wins the collision: don't emit the raw token an
    // alias already maps to, nor one whose name shadows a core alias.
    if (Renderer::coreParamHidesToken(pxr::TfToken(d.token)))
      continue;
    if (!emitted.insert(d.token).second)
      continue;
    view.push_back(ANARIParameter{d.token.c_str(), d.type});
  }

  view.push_back(ANARIParameter{nullptr, ANARI_UNKNOWN});
}

const void *HydraDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  initDevice();
  if (!infoName)
    return nullptr;
  auto &state = *deviceState();
  const std::string_view info = infoName;

  // Device extension reflection: the same per-delegate-honest list the
  // "extension" property serves (SPEC §4.8). A live device is bound to one
  // delegate, so the answer is the same for any objectSubtype asked about.
  const std::vector<const char *> &ext = advertisedExtensions(state);
  if (objectType == ANARI_DEVICE && info == "extension"
      && infoType == ANARI_STRING_LIST)
    return ext.data();

  // Only subtypes the delegate can create answer anything.
  if (!resolveQuerySubtype(objectType, objectSubtype))
    return nullptr;

  if (info == "parameter" && infoType == ANARI_PARAMETER_LIST) {
    // The renderer adds the delegate's render settings (SPEC §11.3).
    if (objectType == ANARI_RENDERER) {
      if (state.rendererParamView.empty())
        buildRendererParamList();
      return state.rendererParamView.data();
    }
    return reflectedParameterList(state, ext, objectType, objectSubtype);
  }
  if (objectType == ANARI_RENDERER && info == "extension"
      && infoType == ANARI_STRING_LIST)
    return ext.data();
  if (objectType == ANARI_FRAME && info == "channel"
      && infoType == ANARI_STRING_LIST)
    return reflectedFrameChannels(state, ext);
  if (info == "sourceExtension" && infoType == ANARI_INT32) {
    const auto *source = static_cast<const char *>(
        query_object_info(objectType, objectSubtype, infoName, ANARI_STRING));
    return sourceExtensionIndex(state, ext, source);
  }
  // description / sourceExtension (string): straight from the generated table.
  return query_object_info(objectType, objectSubtype, infoName, infoType);
}

// Locate a reflected delegate render setting by its verbatim token.
static const RenderSettingDesc *findRenderSetting(
    const HydraGlobalState &state, const char *name)
{
  if (!name)
    return nullptr;
  for (const RenderSettingDesc &d : state.renderSettingDescs)
    if (d.token == name)
      return &d;
  return nullptr;
}

const void *HydraDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  initDevice();
  if (!parameterName || !infoName)
    return nullptr;
  auto &state = *deviceState();
  if (!resolveQuerySubtype(objectType, objectSubtype))
    return nullptr;
  const std::string_view info = infoName;

  // A reflected delegate render setting (SPEC §11.3) carries its descriptor's
  // human name and default, and is never required. Tokens a core alias hides
  // are not reflected, so they answer nothing here either.
  if (objectType == ANARI_RENDERER) {
    const RenderSettingDesc *d = findRenderSetting(state, parameterName);
    if (d && !Renderer::coreParamHidesToken(pxr::TfToken(d->token))) {
      if (d->type != parameterType)
        return nullptr;
      if (info == "description" && infoType == ANARI_STRING)
        return d->humanName.empty() ? d->token.c_str() : d->humanName.c_str();
      if (info == "default" && infoType == d->type) {
        if (d->type == ANARI_STRING)
          return d->defaultString.c_str();
        if (!d->defaultBytes.empty())
          return d->defaultBytes.data();
      }
      if (info == "required" && infoType == ANARI_BOOL) {
        static const int32_t notRequired = 0;
        return &notRequired;
      }
      return nullptr;
    }
  }

  // Everything else answers only for (name, type) pairs the filtered
  // parameter list advertises (SPEC §4.8).
  const std::vector<const char *> &ext = advertisedExtensions(state);
  const ANARIParameter *list =
      reflectedParameterList(state, ext, objectType, objectSubtype);
  if (!reflectsParameter(list, parameterName, parameterType))
    return nullptr;
  if (info == "sourceExtension" && infoType == ANARI_INT32) {
    const auto *source = static_cast<const char *>(query_param_info(objectType,
        objectSubtype,
        parameterName,
        parameterType,
        infoName,
        ANARI_STRING));
    return sourceExtensionIndex(state, ext, source);
  }
  return query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other HydraDevice definitions //////////////////////////////////////////////

HydraDevice::HydraDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<HydraGlobalState>(this_device());
  bindDelegate("default");
  deviceCommitParameters();
}

HydraDevice::HydraDevice(ANARILibrary l, const char *subtype)
    : helium::BaseDevice(l)
{
  m_state = std::make_unique<HydraGlobalState>(this_device());
  bindDelegate(subtype);
  deviceCommitParameters();
}

HydraDevice::~HydraDevice()
{
  auto &state = *deviceState();

  // Let already-queued renders finish, and fire their completion callbacks
  // (SPEC §8.7), while the device is still whole; none fires after
  // anariRelease(device) returns. Renders requested from here on (a callback's
  // re-render chain) are dropped, so this drain terminates.
  state.releasing = true;
  if (state.worker)
    state.worker->postWait([]() {});
  state.commitBuffer.clear();

  reportMessage(ANARI_SEVERITY_DEBUG, "destroying hydra device (%p)", this);

  // The worker owns all Hydra state; joining it (via delete) runs remaining
  // destruction jobs (SPEC §9.6) before the Hgi is torn down.
  delete state.worker;
  state.worker = nullptr;
}

void HydraDevice::bindDelegate(const char *subtype)
{
  auto &state = *deviceState();
  const std::string sub = subtype ? subtype : "default";
  const pxr::TfToken plugin = DelegateRegistry::get().pluginForSubtype(sub);
  state.delegateSubtype = sub;
  state.delegateId = plugin;

  // The delegate's display name keys its registered scene-index filter chain
  // (SPEC §6.1). Storm's plugins register under "GL".
  pxr::HfPluginDesc desc;
  if (pxr::HdRendererPluginRegistry::GetInstance().GetPluginDesc(plugin, &desc))
    state.sceneIndexPluginName = desc.displayName;
}

void HydraDevice::initDevice()
{
  if (m_initialized)
    return;

  auto &state = *deviceState();

  if (state.delegateId.IsEmpty()) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "no Hydra delegate bound to device subtype '%s'",
        state.delegateSubtype.c_str());
  }

  reportMessage(ANARI_SEVERITY_DEBUG,
      "initializing hydra device (%p) -> delegate '%s'",
      this,
      state.delegateId.GetText());

  state.anariDevice = (anari::Device)this;
  state.worker = new Worker();

  // Register the in-memory Hio image plugin so sampler images resolve through
  // the `anari://` asset scheme (SPEC §6.3).
  EnsureAnariHioRegistered();

  m_initialized = true;

  // Probe the delegate's capability lists so the reported extension /
  // object-subtype surface is honest per-delegate (SPEC §4.8).
  probeCapabilities();
}

void HydraDevice::probeCapabilities()
{
  auto &state = *deviceState();
  if (state.capabilitiesProbed)
    return;
  if (!state.worker || state.delegateId.IsEmpty())
    return;

  // The delegate is created and read on the worker thread, which owns the Hgi
  // (SPEC §9.2). Reading the type lists needs no render index / Hgi driver.
  state.worker->postWait([&state]() {
    readDelegateCaps(state.delegateId,
        state.supportedRprims,
        state.supportedSprims,
        state.supportedBprims,
        state.supportedAovChannels,
        state.renderSettingDescs);
  });
  state.capabilitiesProbed = true;

  if (state.supportedRprims.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "could not probe delegate '%s' capabilities; extension list may be "
        "over-reported (SPEC §4.8)",
        state.delegateId.GetText());
  }
}

void HydraDevice::deviceCommitParameters()
{
  helium::BaseDevice::deviceCommitParameters();
}

int HydraDevice::deviceGetProperty(const char *name,
    ANARIDataType type,
    void *mem,
    uint64_t size,
    uint32_t mask)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    // Extension queries can arrive before any object exists; ensure the worker
    // and delegate-capability probe have run (SPEC §4.8), then report the
    // per-delegate-honest list.
    initDevice();
    helium::writeToVoidP(mem, advertisedExtensions(*deviceState()).data());
    return 1;
  } else if (prop == "hydra" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, true);
    return 1;
  } else if (prop == "hydra.shaderNodeCount" && type == ANARI_UINT64) {
    // Every node in the process-global Sdr registry, including source-code
    // nodes (which only the all-versions filter lists). Sdr never frees a
    // node, so this is the observable cost of any shader the device
    // generates (README.md, "Volume costs and limitations").
    helium::writeToVoidP(mem,
        uint64_t(pxr::SdrRegistry::GetInstance()
                     .GetShaderNodeIdentifiers(
                         pxr::TfToken(), pxr::SdrVersionFilterAllVersions)
                     .size()));
    return 1;
  } else if (prop == "hydra.delegate" && type == ANARI_STRING) {
    const std::string &id = deviceState()->delegateId.GetString();
    std::memset(mem, 0, size);
    std::memcpy(
        mem, id.data(), std::min<uint64_t>(id.size(), size ? size - 1 : 0));
    return 1;
  }
  return 0;
}

HydraGlobalState *HydraDevice::deviceState() const
{
  return (HydraGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace anari_hydra
