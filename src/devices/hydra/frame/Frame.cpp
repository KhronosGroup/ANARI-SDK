// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Frame.h"
#include "HydraDevice.h"
#include "hydra/Worker.h"
#include "light/Light.h"
#include "scene/AnariImage.h"
#include "scene/LightBuilder.h"

#include <pxr/imaging/hd/light.h>
#include <pxr/usd/sdf/assetPath.h>

#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hd/bufferArrayRange.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/rprim.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdSt/drawItem.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/selectionTracker.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/capabilities.h>
#include <pxr/imaging/hgi/tokens.h>

#include <pxr/imaging/cameraUtil/framing.h>

#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/glf/simpleLightingContext.h>
#include <pxr/imaging/glf/simpleMaterial.h>

#include <pxr/base/gf/frustum.h>
#include <pxr/base/gf/half.h>
#include <pxr/base/work/loops.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE

namespace anari_hydra {

// Map an ANARI pixel type to the closest Hydra buffer format for honoring an
// arbitrary AOV's requested format via SetRenderOutputSettings (SPEC §11.4).
// UINT32 has no unsigned Hydra format; Int32 is the closest (the ID channels
// already round-trip Int32<->UINT32 by bit-cast, §11.4). HdFormatInvalid means
// "leave the delegate default" (the caller skips the override).
static pxr::HdFormat anariToHdFormat(ANARIDataType t)
{
  switch (t) {
  case ANARI_FLOAT32:
    return pxr::HdFormatFloat32;
  case ANARI_FLOAT32_VEC3:
    return pxr::HdFormatFloat32Vec3;
  case ANARI_FLOAT32_VEC4:
    return pxr::HdFormatFloat32Vec4;
  case ANARI_UINT32:
    return pxr::HdFormatInt32;
  case ANARI_UFIXED8_VEC4:
    return pxr::HdFormatUNorm8Vec4;
  default:
    return pxr::HdFormatInvalid;
  }
}

// Scene-index path of the frame's synthesized ambient dome light (SPEC §6.6).
static const SdfPath kAmbientLightPath("/anari/frame/ambient");

// The ambient dome's 1x1 environment texture, served through the in-memory
// `anari://` asset scheme (SPEC §6.3), holds the fill radiance itself:
// ambientColor * ambientRadiance as one linear float texel, with the dome's
// own intensity and color left at 1. Storm ignores a textureless dome (warns
// "Dome light has no texture asset path" and contributes nothing, proto 016),
// and HdEmbree's dome ignores intensity and color (it lights with the texture
// alone, or 1 without one), so only the texel reaches both alike. Each frame
// owns its image, and every change gets a fresh path: delegates cache
// textures by path.
static std::string AmbientTexPath(uint64_t frameSerial, uint64_t revision)
{
  return "anari://renderer/ambient_f" + std::to_string(frameSerial) + "_r"
      + std::to_string(revision) + ".anari";
}

static void SetAmbientImage(const std::string &path, const GfVec3f &radiance)
{
  AnariImageData texel;
  texel.allocate(1, 1, TexelFormat::Float);
  texel.write(0, helium::float4(radiance[0], radiance[1], radiance[2], 1.f));
  texel.environment = true;
  AnariImageRegistry::instance().set(path, std::move(texel));
}

// Build the uniform ambient dome sprim pointed at its texture. Mirrors the
// hdri dome (Light.cpp) — Storm reads SdfAssetPath::GetResolvedPath()
// directly, so set both the authored and resolved fields.
static LightData BuildAmbientDome(const std::string &texPath)
{
  LightData d;
  d.type = HdPrimTypeTokens->domeLight;
  d.params = {{HdLightTokens->intensity, VtValue(1.f)},
      {HdLightTokens->color, VtValue(GfVec3f(1.f, 1.f, 1.f))},
      {HdLightTokens->textureFile, VtValue(SdfAssetPath(texPath, texPath))}};
  return d;
}

Frame::Frame(HydraGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  // Teardown is Hydra-adjacent: it must run on the worker thread (SPEC §9.6).
  // Self-anchoring (below) guarantees all posted jobs have completed before the
  // last ref drops, so by here the queue holds nothing referencing this frame;
  // we only need to destroy the Hydra objects on the worker. postWait runs
  // inline when the final ref was dropped on the worker itself.
  auto *state = deviceState();
  if (state->worker)
    state->worker->postWait([this]() { teardownHydra(); });
}

HydraGlobalState *Frame::deviceState() const
{
  return asHydraState(m_state);
}

bool Frame::isValid() const
{
  return true;
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  // Last completed frame's render wall-clock, in seconds (SPEC §8.2). The
  // worker thread stamps m_duration; here we just read the atomic — no worker
  // flush, so a HUD polling this every frame never blocks the app thread. Apps
  // query after anariFrameReady, by which point the FIFO worker has written it.
  // `flags` (the ANARI wait mask) is ignored: the atomic already yields a
  // coherent last-frame value. Honoring ANARI_WAIT via a worker flush is a
  // possible future refinement if an app is found querying before frameReady.
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration.load());
    return true;
  }
  // GPU texture bytes held by the frame's delegate after the last completed
  // render (SPEC §8.2), stamped like `duration`. Only delegates that report
  // HdPerfTokens->textureMemory in GetRenderStats (Storm) provide it.
  if (type == ANARI_UINT64 && name == "hydra.textureMemory"
      && m_hasTextureMemory.load()) {
    helium::writeToVoidP(ptr, m_textureMemory.load());
    return true;
  }

  return false;
}

void Frame::commitParameters()
{
  m_world = getParamObject<World>("world");
  m_camera = getParamObject<Camera>("camera");
  m_renderer = getParamObject<Renderer>("renderer");

  const helium::uint2 sz =
      getParam<helium::uint2>("size", helium::uint2(256u, 256u));
  m_width = sz.x;
  m_height = sz.y;

  m_colorType =
      getParam<ANARIDataType>("channel.color", ANARI_UFIXED8_RGBA_SRGB);

  // Standard AOV channels beyond color (SPEC §11.4): a channel is enabled when
  // the frame carries a `channel.<name>` parameter (its value is the requested
  // ANARIDataType). The delegate-native buffer format drives the readback type,
  // so we only track which channels are on, not the requested format.
  m_enabledAovChannels.clear();
  for (const AovChannelDesc &d : standardAovChannels()) {
    if (getParam<ANARIDataType>(d.anariChannel, ANARI_UNKNOWN) != ANARI_UNKNOWN)
      m_enabledAovChannels.insert(d.anariChannel);
  }

  // Arbitrary named Hydra AOVs (SPEC §11.4, ANARI_HYDRA_FRAME_AOV): any frame
  // parameter named `channel.aov:<hydraToken>` requests that Hydra AOV, its
  // value the ANARIDataType map() should deliver (and the buffer format to
  // pin via SetRenderOutputSettings). Enumerate all params by prefix — helium's
  // ParameterizedObject exposes params_begin()/params_end() (as Renderer does).
  m_arbitraryAovs.clear();
  const std::string aovPrefix = "channel.aov:";
  for (auto it = params_begin(); it != params_end(); ++it) {
    const std::string &pname = it->first;
    if (pname.rfind(aovPrefix, 0) != 0)
      continue;
    const std::string token = pname.substr(aovPrefix.size());
    if (token.empty())
      continue;
    const ANARIDataType fmt = getParam<ANARIDataType>(pname, ANARI_UNKNOWN);
    if (fmt != ANARI_UNKNOWN)
      m_arbitraryAovs[token] = fmt;
  }

  // KHR_FRAME_COMPLETION_CALLBACK (SPEC §8.7): takes effect from the next
  // render job, since the commit flush precedes it on the worker queue.
  {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callback = getParam<ANARIFrameCompletionCallback>(
        "frameCompletionCallback", nullptr);
    m_callbackUserData =
        getParam<void *>("frameCompletionCallbackUserData", nullptr);
  }
}

void Frame::finalize()
{
  // Nothing to author; frame consumes committed state at render time.
}

// --- worker-thread section -------------------------------------------------

void Frame::ensureHydra()
{
  if (m_hydraReady)
    return;

  auto *state = deviceState();
  Worker *worker = state->worker;

  // Apply the renderer's settings at delegate creation too (SPEC §4.7), so
  // creation-time-only settings land; per-frame SetRenderSetting below still
  // tracks settings that change after creation. Empty map when no renderer is
  // bound yet (unchanged behavior).
  HdRenderSettingsMap creationSettings;
  if (m_renderer) {
    for (const auto &kv : m_renderer->renderSettings())
      creationSettings[kv.first] = kv.second;
  }
  m_renderDelegate =
      HdRendererPluginRegistry::GetInstance().CreateRenderDelegate(
          state->delegateId, creationSettings);
  if (!m_renderDelegate) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "failed to create Hydra render delegate '%s'",
        state->delegateId.GetText());
    return;
  }

  // channel.normal source (SPEC §11.4): the same resolution the capability
  // probe used, so the frame binds exactly what the device advertised.
  m_normalToken = normalAovToken(m_renderDelegate.Get());

  m_renderIndex.reset(
      HdRenderIndex::New(m_renderDelegate.Get(), {worker->driver()}));

  const SdfPath ctrlPath("/taskController");
  HdRenderDelegate *rd = m_renderDelegate.Get();
  m_taskController = HdxTaskControllerSceneIndex::New({ctrlPath,
      [rd](const TfToken &name) { return rd->GetDefaultAovDescriptor(name); },
      /*isForStorm=*/rd->RequiresStormTasks(),
      /*gpuEnabled=*/true});
  // ANARI aspect and imageRegion already determine the full projection.
  m_taskController->SetOverrideWindowPolicy(CameraUtilDontConform);
  m_renderIndex->InsertSceneIndex(
      m_taskController, ctrlPath, /*needsPrefixing=*/false);

  // The frame-owned ambient scene index (SPEC §4.7/§6.6): holds at most one
  // synthesized uniform dome light. Insert it bare — a dome light needs none of
  // the material-resolving filter chain the world mirror rides. renderOnWorker
  // authors/removes its single prim as ambientRadiance changes.
  m_ambientScene = HdRetainedSceneIndex::New();
  m_renderIndex->InsertSceneIndex(
      m_ambientScene, SdfPath::AbsoluteRootPath(), /*needsPrefixing=*/false);
  m_ambientAuthored = false;

  // Render outputs are set in renderOnWorker (once, then on channel change) so
  // color + any enabled AOV channels land in a single SetRenderOutputs call —
  // re-setting outputs after the first render's binding restarts HdEmbree's
  // progressive pass into an empty frame (SPEC §11.4).
  m_taskController->SetEnablePresentation(false);

  // HdxRenderTaskParams::enableLighting defaults to *false*, under which
  // HdStRenderPassState swaps the simple-lighting shader for its fallback
  // "fully lit" shader — light sprims and material constant params sync
  // correctly but never reach the draw. UsdImagingGLEngine always pushes
  // params with lighting on; do the same so the authored material networks and
  // light sprims (SPEC §6.1/§6.6) actually shade the draw.
  {
    HdxRenderTaskParams renderParams;
    renderParams.enableLighting = true;
    m_taskController->SetRenderParams(renderParams);
  }
  m_taskController->SetCollection(HdRprimCollection(
      HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull)));

  // The task controller's selection task emits benign TF_CODING_ERRORs unless
  // a selection tracker is present in the engine task context (SPEC §8.6).
  m_engine.SetTaskContextData(HdxTokens->selectionState,
      VtValue(std::make_shared<HdxSelectionTracker>()));

  m_hydraReady = true;
}

void Frame::renderOnWorker()
{
  ensureHydra();
  if (!m_hydraReady)
    return; // early-out before t0: leave the previous "last completed" value

  // Time the whole render body (SPEC §8.2 "duration"): stage-2 mirror drain +
  // scene authoring + ambient/lighting/AOV setup + Execute + converge poll.
  // This matches helide_gpu (which times its entire gpu_renderFrame) and
  // answers the app's real question — "how long did this frame take"
  // legitimately includes the reconcile the app waited on. NOT included: the
  // stage-1 commitBuffer.flush() job, which is a separate worker job posted
  // before this one (timing across two jobs isn't worth it for v1).
  const auto t0 = std::chrono::steady_clock::now();

  // Apply the renderer's settings profile to this frame's delegate (SPEC §4.7).
  // Cheap and idempotent, so re-applied each render to track renderer commits.
  if (m_renderer) {
    for (const auto &kv : m_renderer->renderSettings())
      m_renderDelegate->SetRenderSetting(kv.first, kv.second);
  }

  // Dome skydome camera-visibility policy (SPEC §4.7). A dome light OWNS the
  // backdrop under Storm: HdxSkydomeTask is the first render task and paints
  // the dome environment into primary rays, clobbering the `background` clear
  // color. The global HdRenderSettingsTokens->domeLightCameraVisibility lever
  // suppresses that skydome while preserving the dome's light contribution to
  // surfaces. Policy:
  //   - no hdri dome present -> false. Covers the no-dome case AND the ambient-
  //     only case: the synthetic ambient fill dome is never an hdri, so
  //     whenever it is the active dome it is camera-invisible and `background`
  //     owns the backdrop.
  //   - an hdri dome present -> follow the FIRST hdri's `visible` (default
  //   true),
  //     so a default hdri stays visible as the backdrop while `visible=false`
  //     turns it into a pure image-based light and lets `background` show.
  // Only world-direct lights are consulted (an hdri as an instanced-group light
  // is not a meaningful backdrop). More than one hdri: pick the first and warn.
  // Edge case (ambient + hdri both present): two domes, one global lever, and
  // Storm draws a single representative skydome — the first hdri's `visible`
  // wins globally. Accepted; not chased here. Pushed after the renderer
  // settings loop so this policy value wins any verbatim passthrough of the
  // same token (§11.3).
  {
    bool domeVisible = false;
    if (m_world) {
      Light *firstHdri = nullptr;
      int hdriCount = 0;
      for (Light *l : m_world->gatherLights()) {
        if (l && l->isHdri()) {
          if (!firstHdri)
            firstHdri = l;
          ++hdriCount;
        }
      }
      if (firstHdri) {
        domeVisible = firstHdri->visible();
        if (hdriCount > 1)
          reportMessage(ANARI_SEVERITY_WARNING,
              "multiple hdri lights present; following the first light's "
              "`visible` for the global dome camera-visibility (SPEC §4.7)");
      }
    }
    m_renderDelegate->SetRenderSetting(
        HdRenderSettingsTokens->domeLightCameraVisibility,
        VtValue(domeVisible));
  }

  // Stage-2 drain (SPEC §9.3): author the mirror from committed state, but only
  // when stage-1 finalize marked something into the mirror-dirty set (SPEC
  // §5.2) — an idle re-render of an unchanged scene skips the reconcile.
  // Fine-grained mark-driven authoring (per-object dependent-prim maps) is a
  // later refinement; the reconcile itself is idempotent.
  std::unordered_set<Object *> drained;
  deviceState()->mirrorDirty.drain(drained);
  if (!drained.empty())
    ++deviceState()->mirrorRevision;

  if (m_insertedWorldScene
      && (!m_world || m_insertedWorldSerial != m_world->serial())) {
    m_renderIndex->RemoveSceneIndex(m_insertedWorldScene);
    m_insertedWorldScene = nullptr;
    m_insertedWorldSerial = 0;
  }
  if (m_world) {
    const bool firstInsert = (m_insertedWorldSerial != m_world->serial());
    if (m_mirrorRevision != deviceState()->mirrorRevision || firstInsert) {
      m_world->mirror()->sync(m_world.ptr);
      m_mirrorRevision = deviceState()->mirrorRevision;
    }

    // Insert this world's mirror into our render index (once per world). The
    // mirror passes through the delegate's registered scene-index filter chain
    // (node-identifier / material-binding resolving, material primvar transfer,
    // implicit-surface tessellation, …) exactly as UsdImagingGLEngine does — a
    // bare insert skips the filters Storm's material pipeline expects (SPEC
    // §6.1).
    if (firstInsert) {
      HdSceneIndexBaseRefPtr filtered =
          HdSceneIndexPluginRegistry::GetInstance()
              .AppendSceneIndicesForRenderer(
                  deviceState()->sceneIndexPluginName,
                  m_world->mirror()->sceneIndex());
      m_renderIndex->InsertSceneIndex(
          filtered, SdfPath::AbsoluteRootPath(), /*needsPrefixing=*/false);
      m_insertedWorldScene = filtered;
      m_insertedWorldSerial = m_world->serial();
    }
  }

  // Ambient fill (SPEC §4.7/§6.6): the renderer's ambientRadiance/ambientColor
  // synthesize a uniform dome light in the frame-owned ambient scene index. A
  // retained AddPrims replaces in place, so re-author only when radiance
  // crosses 0 or the intensity/color changes; remove the sprim when radiance
  // drops to 0. (Scene-ambient via GlfSimpleLightingContext does NOT reach
  // UsdPreviewSurface under Storm — proto 016 — hence a real dome-light sprim.)
  const float ambientRadiance =
      m_renderer ? m_renderer->ambientRadiance() : 0.f;
  const GfVec3f ambientColor = m_renderer
      ? GfVec3f(m_renderer->ambientColor().x,
            m_renderer->ambientColor().y,
            m_renderer->ambientColor().z)
      : GfVec3f(1.f, 1.f, 1.f);
  const bool wantAmbient = ambientRadiance > 0.f;
  if (m_ambientScene) {
    if (wantAmbient) {
      if (!m_ambientAuthored || ambientRadiance != m_ambientAuthoredRadiance
          || ambientColor != m_ambientAuthoredColor) {
        static std::atomic<uint64_t> s_nextFrameSerial{0};
        if (m_ambientFrameSerial == 0)
          m_ambientFrameSerial = ++s_nextFrameSerial;
        AnariImageRegistry::instance().erase(m_ambientTexPath);
        m_ambientTexPath =
            AmbientTexPath(m_ambientFrameSerial, ++m_ambientRevision);
        SetAmbientImage(m_ambientTexPath, ambientColor * ambientRadiance);
        const LightData dome = BuildAmbientDome(m_ambientTexPath);
        m_ambientScene->AddPrims(
            {{kAmbientLightPath, dome.type, BuildLightPrim(dome)}});
        m_ambientAuthored = true;
        m_ambientAuthoredRadiance = ambientRadiance;
        m_ambientAuthoredColor = ambientColor;
      }
    } else if (m_ambientAuthored) {
      m_ambientScene->RemovePrims({{kAmbientLightPath}});
      m_ambientAuthored = false;
      AnariImageRegistry::instance().erase(m_ambientTexPath);
      m_ambientTexPath.clear();
    }
  }

  // Lighting (SPEC §6.6): real ANARI lights are authored as UsdLux sprims and
  // consumed by the delegate's light task; only a lightless world falls back to
  // a simple headlight so geometry stays visible. An empty lighting state when
  // lights exist avoids double-lighting from a stale headlight. The ambient
  // fill above counts as app-supplied lighting, so it too drops the headlight
  // (SPEC §6.8 option b): once ambient is set the app HAS lit the scene, and
  // layering an unrequested camera light contradicts the ambient-only intent.
  {
    const bool hasLights = (m_world && m_world->hasAnyLight()) || wantAmbient;
    GlfSimpleLightingContextRefPtr lighting = GlfSimpleLightingContext::New();
    if (!hasLights) {
      GlfSimpleLight headlight;
      headlight.SetPosition(GfVec4f(0, 0, 5, 1));
      headlight.SetAmbient(GfVec4f(0.3f, 0.3f, 0.3f, 1));
      lighting->SetLights({headlight});
      lighting->SetSceneAmbient(GfVec4f(0.2f, 0.2f, 0.2f, 1));
    }
    lighting->SetMaterial(GlfSimpleMaterial());
    m_taskController->SetLightingState(lighting);

    // The same policy for delegates that gate scene lights behind a render
    // setting: HdEmbree's `enableLighting` defaults to false, under which it
    // ignores every light sprim and shades with its own camera light. Turn it
    // on whenever the world (or the ambient fill) supplies lighting, and leave
    // a lightless world on the camera light, the counterpart of Storm's
    // headlight above. An app that sets `enableLighting` itself (§11.3
    // passthrough) keeps its value. Delegates without the setting ignore it.
    static const TfToken kEnableLighting("enableLighting");
    if (!m_renderer || !m_renderer->renderSettings().count(kEnableLighting))
      m_renderDelegate->SetRenderSetting(kEnableLighting, VtValue(hasLights));
  }

  // AOV outputs (SPEC §11.4). SetRenderOutputs rewires the AOV bprims, so only
  // call it once (first render) and thereafter when the enabled channel set
  // changes (mirrors the resize guard). The guard key is the FULL output set:
  // the enabled standard channels ∪ the arbitrary `channel.aov:` tokens. Reset
  // the framing cache on change so the freshly created bprims get sized.
  if (!m_outputsSet || m_configuredAovChannels != m_enabledAovChannels
      || m_configuredArbitraryAovs != m_arbitraryAovs) {
    m_outputsSet = true;
    TfTokenVector outputs;
    outputs.reserve(m_enabledAovChannels.size() + m_arbitraryAovs.size() + 1);
    outputs.push_back(HdAovTokens->color);
    for (const AovChannelDesc &d : standardAovChannels()) {
      if (m_enabledAovChannels.count(d.anariChannel))
        outputs.push_back(boundAovToken(d));
    }
    // Arbitrary named AOVs (SPEC §11.4): bind each requested Hydra token.
    for (const auto &kv : m_arbitraryAovs)
      outputs.push_back(TfToken(kv.first));
    if (m_enabledAovChannels.count("channel.primitiveId")
        || m_enabledAovChannels.count("channel.instanceId"))
      outputs.push_back(HdAovTokens->primId);
    std::sort(outputs.begin(), outputs.end());
    outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    m_taskController->SetRenderOutputs(outputs);

    // Honor each arbitrary AOV's requested ANARI format on its bprim via
    // SetRenderOutputSettings (SPEC §8.1/§11.4) — must come after
    // SetRenderOutputs, which creates the binding this overrides. Start from
    // the delegate's default descriptor (clear value / multisampling) and pin
    // only the format; an unmapped ANARI type keeps the delegate default.
    for (const auto &kv : m_arbitraryAovs) {
      const TfToken tok(kv.first);
      const HdFormat fmt = anariToHdFormat(kv.second);
      if (fmt == HdFormatInvalid)
        continue;
      HdAovDescriptor desc = m_renderDelegate->GetDefaultAovDescriptor(tok);
      desc.format = fmt;
      m_taskController->SetRenderOutputSettings(tok, desc);
    }

    // Standard normal channel (SPEC §11.4): misses read (0,0,0) on every
    // delegate (HdEmbree clears `normal` to -1), and Storm's Neye target is
    // pinned to Float32Vec4 — Storm writes the raw eye-space normal as
    // vec4(Neye, 1), which its default UNorm8Vec4 target would clamp (negative
    // components) and quantize. Applied after the arbitrary pins so the
    // standard channel's contract wins over a concurrent `channel.aov:Neye`
    // (whose readback then only matches a FLOAT32_VEC4 request).
    if (m_enabledAovChannels.count("channel.normal")
        && !m_normalToken.IsEmpty()) {
      const TfToken tok = m_normalToken;
      HdAovDescriptor desc = m_renderDelegate->GetDefaultAovDescriptor(tok);
      if (tok == HdAovTokens->Neye)
        desc.format = HdFormatFloat32Vec4;
      if (desc.format == HdFormatFloat32Vec4)
        desc.clearValue = VtValue(GfVec4f(0.f));
      else if (desc.format == HdFormatFloat32Vec3)
        desc.clearValue = VtValue(GfVec3f(0.f));
      m_taskController->SetRenderOutputSettings(tok, desc);
    }

    m_configuredAovChannels = m_enabledAovChannels;
    m_configuredArbitraryAovs = m_arbitraryAovs;
    m_configuredWidth = 0;
    m_configuredHeight = 0;
  }

  // Background clear color (SPEC §4.7): the renderer's core `background`
  // (FLOAT32_VEC4) becomes the color AOV's clear value — the color shown where
  // primary rays miss geometry (honored only where no dome paints the backdrop,
  // per the domeLightCameraVisibility policy above). Applied each render
  // (cheap, idempotent, like the per-frame SetRenderSetting loop) so it both
  // tracks a changed `background` — which can change without the channel set
  // changing, so it must NOT sit behind the SetRenderOutputs guard — and
  // survives the SetRenderOutputs above, which recreates the color binding from
  // the delegate's default descriptor and would otherwise wipe the clear value.
  // Start from that default descriptor and override only clearValue (matching
  // the arbitrary-AOV format path). GfVec4f clearValue matches usdImaging
  // convention regardless of the delegate's color buffer format.
  //
  // A background IMAGE (KHR_RENDERER_BACKGROUND_IMAGE) instead clears the color
  // AOV to transparent black, so the rendered buffer holds only the scene (and
  // any visible dome skydome, which writes opaque alpha) as premultiplied
  // colour; readbackColor then composites the image under it. The image is
  // snapshotted here so the composite matches this render.
  m_backgroundImage = m_renderer ? m_renderer->backgroundImage() : nullptr;
  if (m_renderer) {
    const helium::float4 bg = m_backgroundImage
        ? helium::float4(0.f, 0.f, 0.f, 0.f)
        : m_renderer->background();
    HdAovDescriptor desc =
        m_renderDelegate->GetDefaultAovDescriptor(HdAovTokens->color);
    desc.clearValue = VtValue(GfVec4f(bg.x, bg.y, bg.z, bg.w));
    m_taskController->SetRenderOutputSettings(HdAovTokens->color, desc);
  }

  // Resize / framing.
  if (m_configuredWidth != m_width || m_configuredHeight != m_height) {
    m_taskController->SetRenderBufferSize(GfVec2i(m_width, m_height));
    m_taskController->SetFraming(CameraUtilFraming(
        GfRect2i(GfVec2i(0, 0), int(m_width), int(m_height))));
    m_configuredWidth = m_width;
    m_configuredHeight = m_height;
  }

  // Camera.
  const double aspect = m_height ? double(m_width) / double(m_height) : 1.0;
  m_orthographic = m_camera && m_camera->isOrthographic();
  if (m_camera) {
    m_viewMatrix = m_camera->viewMatrix();
    const GfMatrix4d projection = m_camera->projectionMatrix(
        aspect, m_world ? m_world->bounds(true) : Bounds());
    m_inverseProjection = projection.GetInverse();
    m_taskController->SetFreeCameraMatrices(m_camera->viewMatrix(), projection);
  } else {
    GfMatrix4d view;
    view.SetLookAt(GfVec3d(0, 0, 5), GfVec3d(0, 0, 0), GfVec3d(0, 1, 0));
    m_viewMatrix = view;
    GfFrustum frustum;
    frustum.SetPerspective(45.0, true, aspect, 0.01, 1000.0);
    const GfMatrix4d projection = frustum.ComputeProjectionMatrix();
    m_inverseProjection = projection.GetInverse();
    m_taskController->SetFreeCameraMatrices(view, projection);
  }

  // Diff-and-kick render (usdrecord pattern, SPEC §8.3). One Execute dispatches
  // the render; a one-Execute delegate (Storm) then reports converged
  // immediately, while a progressive delegate (HdEmbree) renders on a
  // background thread. The first Execute after (re)binding outputs restarts the
  // pass and marks the AOV buffers unconverged, so poll convergence AFTER that
  // dispatch — with a short sleep and a wall-clock budget, not a fixed spin
  // count (a fixed count can exhaust before the async render finishes, reading
  // back empty).
  const SdfPathVector taskPaths = m_taskController->GetRenderingTaskPaths();
  m_engine.Execute(m_renderIndex.get(), taskPaths);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!m_engine.AreTasksConverged(m_renderIndex.get(), taskPaths)) {
    if (std::chrono::steady_clock::now() > deadline) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "render did not converge within the time budget; presenting the "
          "latest frame (SPEC §8.3)");
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    m_engine.Execute(m_renderIndex.get(), taskPaths);
  }

  // Freeze IDs with the rendered image, independent of later commits or frames.
  m_pickingIds.clear();
  if (m_world) {
    for (const auto &entry : m_world->mirror()->pickingIds()) {
      if (const auto *prim = m_renderIndex->GetRprim(entry.first))
        m_pickingIds.emplace(prim->GetPrimId(), entry.second);
    }
  }
  renderPicking();
  const VtDictionary stats = m_renderDelegate->GetRenderStats();
  const auto texMem = stats.find(HdPerfTokens->textureMemory.GetString());
  if (texMem != stats.end() && texMem->second.IsHolding<size_t>()) {
    m_textureMemory = uint64_t(texMem->second.UncheckedGet<size_t>());
    m_hasTextureMemory = true;
  }
  // Stamped here, before completeOnWorker sets m_rendered, so any observer
  // gating on m_rendered sees a consistent duration (SPEC §8.2).
  const auto t1 = std::chrono::steady_clock::now();
  m_duration = std::chrono::duration<float>(t1 - t0).count();
}

void Frame::completeOnWorker()
{
  // Ready before the callback, so the callback (and anyone it signals)
  // observes a ready frame. A failed render (no delegate) completes too: an
  // app polling NO_WAIT or waiting on its callback must not hang.
  m_rendered = true;

  ANARIFrameCompletionCallback cb = nullptr;
  const void *userData = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    cb = m_callback;
    userData = m_callbackUserData;
  }
  // Invoked last and outside any device lock: the callback may re-render this
  // frame (posting a job behind this one) or map/query it. The render job's
  // self-anchor keeps the frame alive for the call even if the app released
  // it. helium lets the callback's calls on this frame skip its object lock,
  // which an app thread blocked in frameReady(WAIT) or map on it holds.
  invokeCompletionCallback(cb, userData, deviceState()->anariDevice);
}

void Frame::teardownHydra()
{
  // Strict order (SPEC §9.6): task controller, then render index (prims
  // finalize against a still-live delegate), then delegate.
  // First drop the engine's task-context data: Execute stashes the simple-
  // lighting shader there, and a dome light's environment texture keeps GPU
  // sampler/texture handles whose destructors touch the Storm resource registry
  // — which the render index owns. Release them while that registry is still
  // alive, else the handles dangle on it at ~HdEngine (SPEC §6.3/§9.6).
  m_engine.ClearTaskContextData();
  m_taskController = nullptr;
  m_insertedWorldScene = nullptr;
  m_insertedWorldSerial = 0;
  m_ambientScene = nullptr; // owned by the render index; drop our ref too
  m_renderIndex.reset();
  m_renderDelegate = HdPluginRenderDelegateUniqueHandle();
  m_hydraReady = false;
  m_ambientAuthored = false;
  AnariImageRegistry::instance().erase(m_ambientTexPath);
  m_ambientTexPath.clear();
}

// --- app-facing section ----------------------------------------------------

void Frame::renderFrame()
{
  auto *state = deviceState();
  if (state->releasing) {
    // A completion callback re-rendering during anariRelease(device) (SPEC
    // §8.7): drop it so the release drain terminates.
    reportMessage(ANARI_SEVERITY_WARNING,
        "anariRenderFrame during device release ignored");
    return;
  }
  m_rendered = false;

  // Stage 1 (device-side commit application) then the render job, FIFO on the
  // worker. The render job self-anchors an internal ref on this frame so an
  // anariRelease(frame) before the job runs cannot free it out from under the
  // worker (the SPEC §10.3 anchor-registry guarantee, applied per job here).
  helium::IntrusivePtr<Frame> self(this);
  state->worker->post([state]() { state->commitBuffer.flush(); });
  state->worker->post([self]() {
    self.ptr->renderOnWorker();
    self.ptr->completeOnWorker();
  });
}

static inline uint8_t ToU8(float v)
{
  v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
  return static_cast<uint8_t>(v * 255.f + 0.5f);
}

// Linear → sRGB transfer curve (IEC 61966-2-1). The delegate's color AOV holds
// linear radiance; an sRGB channel type asks for encoded bytes, so the encode
// happens here on map (SPEC §8.1 "sRGB convert-on-map"). RGB only — alpha
// stays linear.
static inline float LinearToSrgb(float v)
{
  v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
  return v <= 0.0031308f ? v * 12.92f
                         : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}

// LUT'd encode for the per-pixel hot path: std::pow per component is the
// readback bottleneck. 8192 entries keep the error under half an 8-bit LSB
// even at the curve's steepest point (slope 12.92 near black: half a bucket
// is 12.92 * 255 / (2 * 8191) ≈ 0.2 LSB).
static inline uint8_t LinearToSrgbU8(float v)
{
  constexpr int N = 8192;
  static const std::vector<uint8_t> lut = [] {
    std::vector<uint8_t> t(N);
    for (int i = 0; i < N; ++i)
      t[i] = ToU8(LinearToSrgb(float(i) / (N - 1)));
    return t;
  }();
  v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
  return lut[int(v * (N - 1) + 0.5f)];
}

// Sample a background image (KHR_RENDERER_BACKGROUND_IMAGE, SPEC §4.7) the way
// VisRTX does: the image is stretched over the frame, uv in [0,1]^2 with row 0
// of the image on row 0 of the frame (ANARI's bottom row), bilinear filtering
// with texel centres at (i + 0.5) / n, and clamp-to-edge wrapping.
static void SampleBackground(
    const AnariImageData &img, float u, float v, float out[4])
{
  const float fx = u * img.width - 0.5f, fy = v * img.height - 0.5f;
  const float x0f = std::floor(fx), y0f = std::floor(fy);
  const float tx = fx - x0f, ty = fy - y0f;
  const int x0 = int(x0f), y0 = int(y0f);
  auto texel = [&](int x, int y) {
    x = std::clamp(x, 0, img.width - 1);
    y = std::clamp(y, 0, img.height - 1);
    return img.read(size_t(y) * img.width + x);
  };
  const anari::math::float4 a = texel(x0, y0), b = texel(x0 + 1, y0);
  const anari::math::float4 c = texel(x0, y0 + 1), d = texel(x0 + 1, y0 + 1);
  const anari::math::float4 r =
      (a * (1.f - tx) + b * tx) * (1.f - ty) + (c * (1.f - tx) + d * tx) * ty;
  out[0] = r.x;
  out[1] = r.y;
  out[2] = r.z;
  out[3] = r.w;
}

// Read the color AOV back into m_mappedColor, converting to m_colorType. Runs
// on the worker thread (where the Hgi lives, SPEC §8.5). A background image is
// composited under the rendered (premultiplied) colour first, in float before
// any 8-bit/sRGB encode, matching VisRTX's non-premultiplied-background
// "under": rgb = C.rgb + (1 - C.a) * bg.rgb, a = C.a + (1 - C.a) * bg.a.
void Frame::readbackColor()
{
  if (!m_hydraReady || !m_taskController)
    return;
  HdRenderBuffer *colorBuf = static_cast<HdRenderBuffer *>(
      m_renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer,
          m_taskController->GetRenderBufferPath(HdAovTokens->color)));
  if (!colorBuf)
    return;

  colorBuf->Resolve();
  void *data = colorBuf->Map();
  const size_t n = size_t(colorBuf->GetWidth()) * colorBuf->GetHeight();
  const HdFormat fmt = colorBuf->GetFormat();

  // Convert delegate output to the requested ANARI color format.
  const bool wantFloat = (m_colorType == ANARI_FLOAT32_VEC4);
  const bool wantSrgb = (m_colorType == ANARI_UFIXED8_RGBA_SRGB);
  m_mappedColor.resize(n * (wantFloat ? 16 : 4));

  auto readPixel = [&](size_t i, float out[4]) {
    if (fmt == HdFormatFloat16Vec4) {
      const GfHalf *p = static_cast<const GfHalf *>(data) + i * 4;
      for (int k = 0; k < 4; ++k)
        out[k] = float(p[k]);
    } else if (fmt == HdFormatFloat32Vec4) {
      const float *p = static_cast<const float *>(data) + i * 4;
      for (int k = 0; k < 4; ++k)
        out[k] = p[k];
    } else if (fmt == HdFormatUNorm8Vec4) {
      const uint8_t *p = static_cast<const uint8_t *>(data) + i * 4;
      for (int k = 0; k < 4; ++k)
        out[k] = p[k] / 255.f;
    } else {
      for (int k = 0; k < 4; ++k)
        out[k] = 0.f;
    }
  };

  const AnariImageData *bgImage = m_backgroundImage.get();
  const uint32_t bufW = colorBuf->GetWidth();
  const float invW = bufW ? 1.f / bufW : 0.f;
  const float invH = colorBuf->GetHeight() ? 1.f / colorBuf->GetHeight() : 0.f;

  // Per-pixel and independent, so convert in parallel (the sRGB encode's
  // std::pow dominates); WorkParallelForN runs serially under small n.
  WorkParallelForN(n, [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      float px[4];
      readPixel(i, px);
      if (bgImage) {
        float bg[4];
        SampleBackground(*bgImage,
            (float(i % bufW) + 0.5f) * invW,
            (float(i / bufW) + 0.5f) * invH,
            bg);
        const float t = 1.f - px[3];
        for (int k = 0; k < 4; ++k)
          px[k] += t * bg[k];
      }
      if (wantFloat) {
        std::memcpy(m_mappedColor.data() + i * 16, px, 16);
      } else {
        uint8_t *dst = m_mappedColor.data() + i * 4;
        for (int k = 0; k < 3; ++k)
          dst[k] = wantSrgb ? LinearToSrgbU8(px[k]) : ToU8(px[k]);
        dst[3] = ToU8(px[3]);
      }
    }
  });
  colorBuf->Unmap();
}

TfToken Frame::boundAovToken(const AovChannelDesc &desc) const
{
  if (desc.hydraToken == HdAovTokens->normal && !m_normalToken.IsEmpty())
    return m_normalToken;
  return desc.hydraToken;
}

// channel.normal from Storm's Neye target (SPEC §11.4). Neye holds the shading
// normal in eye space (Float32Vec4, alpha 1 where a fragment landed, the zero
// clear elsewhere). Rotate it back through the view matrix that rendered this
// frame — under Gf's row-vector convention n_eye = n_world * (V^-1)^T, so
// n_world = n_eye * V^T — and renormalize; misses stay (0,0,0).
void Frame::readbackNormalFromNeye(std::vector<uint8_t> &out)
{
  std::vector<uint8_t> eye;
  readbackAovBuffer(HdAovTokens->Neye, ANARI_FLOAT32_VEC4, eye);
  const size_t n = eye.size() / (4 * sizeof(float));
  out.assign(n * 3 * sizeof(float), 0);
  const GfMatrix4d eyeToWorldNormal = m_viewMatrix.GetTranspose();
  WorkParallelForN(n, [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      float p[4];
      std::memcpy(p, eye.data() + i * 4 * sizeof(float), sizeof(p));
      if (p[3] == 0.f)
        continue;
      GfVec3d w = eyeToWorldNormal.TransformDir(GfVec3d(p[0], p[1], p[2]));
      const double len = w.GetLength();
      if (!(len > 0.0) || !std::isfinite(len))
        continue;
      w /= len;
      const float world[3] = {float(w[0]), float(w[1]), float(w[2])};
      std::memcpy(out.data() + i * 3 * sizeof(float), world, sizeof(world));
    }
  });
}

// Read a standard AOV channel back into `out` (SPEC §11.4). Depth becomes
// distance along the camera ray; normal is a world-space Float32Vec3 (the
// delegate's `normal` AOV, or Storm's Neye rotated to world space). The ID
// channels translate delegate indices through the rendered ANARI identity
// snapshot. If the delegate hands back an unexpected format, zero-fill so the
// map stays well-formed.
void Frame::readbackAov(const AovChannelDesc &desc, std::vector<uint8_t> &out)
{
  if (desc.pixelType == ANARI_UINT32) {
    readbackPicking(desc.hydraToken, out);
    return;
  }
  const TfToken bound = boundAovToken(desc);
  if (bound == HdAovTokens->Neye) {
    readbackNormalFromNeye(out);
    return;
  }
  readbackAovBuffer(bound, desc.pixelType, out);
  if (desc.hydraToken != HdAovTokens->depth || out.empty())
    return;

  // Hydra depth is normalized window depth. Unproject pixel centers with the
  // exact projection that rendered this buffer, so clipping and resize changes
  // do not require a second copy of the camera's projection formula. Only the
  // standard channel is converted; channel.aov:depth remains delegate-native.
  const size_t n = out.size() / sizeof(float);
  WorkParallelForN(n, [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      float depth;
      std::memcpy(&depth, out.data() + i * sizeof(float), sizeof(float));
      float distance = std::numeric_limits<float>::infinity();
      if (std::isfinite(depth) && depth >= 0.f && depth < 1.f) {
        const GfVec3d ndc(
            2.0 * (i % m_configuredWidth + 0.5) / m_configuredWidth - 1.0,
            2.0 * (i / m_configuredWidth + 0.5) / m_configuredHeight - 1.0,
            2.0 * depth - 1.0);
        const auto eyePoint = m_inverseProjection.Transform(ndc);
        // Orthographic rays start on the camera plane at each pixel's x/y.
        distance = float(m_orthographic ? -eyePoint[2] : eyePoint.GetLength());
      }
      std::memcpy(out.data() + i * sizeof(float), &distance, sizeof(float));
    }
  });
}

void Frame::renderPicking()
{
  m_stormPrimitiveIds.clear();
  m_stormObjectIds.clear();
  m_stormInstanceIds.clear();
  if (!m_renderDelegate->RequiresStormTasks()
      || !m_enabledAovChannels.count("channel.primitiveId"))
    return;
  m_stormPrimitiveIds.assign(size_t(m_width) * m_height, ~0u);
  m_stormObjectIds.assign(m_stormPrimitiveIds.size(), ~0u);
  m_stormInstanceIds.assign(m_stormPrimitiveIds.size(), ~0u);
  HdxPickHitVector hits;
  HdxPickTaskContextParams params;
  params.resolution = GfVec2i(m_width, m_height);
  params.resolveMode = HdxPickTokens->resolveAll;
  params.pickTarget = HdxPickTokens->pickPrimsAndInstances;
  params.viewMatrix = m_viewMatrix;
  params.projectionMatrix = m_inverseProjection.GetInverse();
  params.collection = HdRprimCollection(
      HdTokens->geometry, HdReprSelector(HdReprTokens->smoothHull));
  params.outHits = &hits;
  m_engine.SetTaskContextData(HdxPickTokens->pickParams, VtValue(params));
  HdTaskSharedPtrVector tasks;
  for (const auto &path : m_taskController->GetPickingTaskPaths())
    tasks.push_back(m_renderIndex->GetTask(path));
  m_engine.Execute(m_renderIndex.get(), &tasks);
  // The context carries a pointer to the local hit vector; remove it now.
  m_engine.RemoveTaskContextData(HdxPickTokens->pickParams);
  const GfMatrix4d worldToClip = params.viewMatrix * params.projectionMatrix;
  for (const auto &hit : hits) {
    const auto *prim = m_renderIndex->GetRprim(hit.objectId);
    if (!prim)
      continue;
    const auto it = m_pickingIds.find(prim->GetPrimId());
    if (it == m_pickingIds.end())
      continue;
    int element = hit.elementIndex >= 0 ? hit.elementIndex : hit.pointIndex;
    if (hit.elementIndex < 0 && hit.pointIndex >= 0) {
      const Hgi *hgi = deviceState()->worker->hgi();
      if (hgi->GetAPIName() == HgiTokens->OpenGL
          && hgi->GetCapabilities()->IsSet(
              HgiDeviceCapabilitiesBitsShaderDrawParameters)) {
        // For OpenGL nonindexed draws BaseVertex is zero, while VertexID
        // includes the first vertex. Decode Storm's point ID with its BAR
        // offset.
        const auto &items = prim->GetDrawItems(HdReprTokens->smoothHull);
        if (!items.empty()) {
          const auto *item =
              dynamic_cast<const HdStDrawItem *>(items.front().get());
          if (item && item->GetVertexPrimvarRange())
            element -= item->GetVertexPrimvarRange()->GetElementOffset();
        }
      }
    }
    if (element < 0 || size_t(element) >= it->second.primitives.size())
      continue;
    // HdxPickResult unprojects x/width and y/height, without a half-pixel
    // offset.
    const auto ndc = worldToClip.Transform(hit.worldSpaceHitPoint);
    const long x = std::lround((ndc[0] + 1.0) * 0.5 * m_width);
    const long y = std::lround((ndc[1] + 1.0) * 0.5 * m_height);
    if (x >= 0 && y >= 0 && x < m_width && y < m_height) {
      const size_t pixel = size_t(y) * m_width + x;
      m_stormPrimitiveIds[pixel] = it->second.primitives[element];
      m_stormObjectIds[pixel] = it->second.object;
      if (it->second.perInstanceCopy)
        m_stormInstanceIds[pixel] = it->second.instances[0];
      else if (hit.instanceIndex >= 0
          && size_t(hit.instanceIndex) < it->second.instances.size())
        m_stormInstanceIds[pixel] = it->second.instances[hit.instanceIndex];
    }
  }
}

// Raw Hydra element/instance indices need the owning rprim to resolve ANARI
// IDs.
void Frame::readbackPicking(const TfToken &token, std::vector<uint8_t> &out)
{
  if (m_renderDelegate->RequiresStormTasks() && !m_stormPrimitiveIds.empty()) {
    const auto &ids = token == HdAovTokens->primId ? m_stormObjectIds
        : token == HdAovTokens->instanceId         ? m_stormInstanceIds
                                                   : m_stormPrimitiveIds;
    out.resize(ids.size() * sizeof(uint32_t));
    std::memcpy(out.data(), ids.data(), out.size());
    return;
  }
  std::vector<uint8_t> objects;
  readbackAovBuffer(HdAovTokens->primId, ANARI_UINT32, objects);
  readbackAovBuffer(token, ANARI_UINT32, out);
  for (size_t i = 0; i + sizeof(uint32_t) <= out.size();
       i += sizeof(uint32_t)) {
    uint32_t value = ~0u;
    int object = -1;
    uint32_t index;
    std::memcpy(&index, out.data() + i, sizeof(index));
    if (i + sizeof(object) <= objects.size())
      std::memcpy(&object, objects.data() + i, sizeof(object));
    const auto it = m_pickingIds.find(object);
    if (it != m_pickingIds.end()) {
      const auto &ids = it->second;
      if (token == HdAovTokens->primId)
        value = ids.object;
      else {
        const auto &primitives =
            deviceState()->delegateId == TfToken("HdEmbreeRendererPlugin")
            ? ids.triangles
            : ids.primitives;
        const auto &values =
            token == HdAovTokens->elementId ? primitives : ids.instances;
        if (token == HdAovTokens->instanceId && ids.perInstanceCopy)
          value = ids.instances[0];
        else if (index < values.size())
          value = values[index];
      }
    }
    std::memcpy(out.data() + i, &value, sizeof(value));
  }
}

// Read a Hydra AOV bprim back into `out`, converting to `pixelType` (SPEC
// §11.4). Shared by the standard-channel and arbitrary-named (channel.aov:)
// paths. depth/normal are the delegate's Float32/Float32Vec3 buffers surfaced
// as FLOAT32/FLOAT32_VEC3; the ID / UINT32 channels reinterpret the Int32
// buffer verbatim (background -1 → 0xFFFFFFFF, preserving "no hit"). Because
// arbitrary AOVs pin their buffer format via SetRenderOutputSettings, the
// delegate format usually already matches the requested type; when it does not
// (delegate could not honor it), zero-fill so the map stays well-formed.
void Frame::readbackAovBuffer(const TfToken &hydraToken,
    ANARIDataType pixelType,
    std::vector<uint8_t> &out)
{
  if (!m_hydraReady || !m_taskController)
    return;
  HdRenderBuffer *buf = static_cast<HdRenderBuffer *>(
      m_renderIndex->GetBprim(HdPrimTypeTokens->renderBuffer,
          m_taskController->GetRenderBufferPath(hydraToken)));
  if (!buf)
    return;

  buf->Resolve();
  const void *data = buf->Map();
  const size_t n = size_t(buf->GetWidth()) * buf->GetHeight();
  const HdFormat fmt = buf->GetFormat();

  if (pixelType == ANARI_FLOAT32) {
    out.assign(n * sizeof(float), 0);
    if (fmt == HdFormatFloat32)
      std::memcpy(out.data(), data, n * sizeof(float));
  } else if (pixelType == ANARI_FLOAT32_VEC3) {
    out.assign(n * 3 * sizeof(float), 0);
    if (fmt == HdFormatFloat32Vec3)
      std::memcpy(out.data(), data, n * 3 * sizeof(float));
  } else if (pixelType == ANARI_FLOAT32_VEC4) {
    out.assign(n * 4 * sizeof(float), 0);
    if (fmt == HdFormatFloat32Vec4)
      std::memcpy(out.data(), data, n * 4 * sizeof(float));
  } else if (pixelType == ANARI_UFIXED8_VEC4) {
    out.assign(n * 4, 0);
    if (fmt == HdFormatUNorm8Vec4)
      std::memcpy(out.data(), data, n * 4);
  } else { // ANARI_UINT32 IDs: reinterpret the Int32 buffer (bit-cast).
    out.assign(n * sizeof(uint32_t), 0);
    if (fmt == HdFormatInt32)
      std::memcpy(out.data(), data, n * sizeof(uint32_t));
  }
  buf->Unmap();
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  *width = m_width;
  *height = m_height;

  auto *state = deviceState();

  if (channel == "channel.color") {
    *pixelType = m_colorType;
    // Draining the worker queue guarantees the render job has completed (FIFO),
    // then the readback runs on the worker where the Hgi lives (SPEC §8.5).
    state->worker->postWait([this]() { readbackColor(); });
    return m_mappedColor.data();
  }

  const std::string name(channel);

  // An arbitrary named Hydra AOV (SPEC §11.4, ANARI_HYDRA_FRAME_AOV):
  // `channel.aov:<hydraToken>`. Map only if it was requested this frame; the
  // buffer is read back in the requested format (pinned via
  // SetRenderOutputSettings). Return nullptr for an unrequested token.
  {
    static const std::string aovPrefix = "channel.aov:";
    if (name.rfind(aovPrefix, 0) == 0) {
      const std::string token = name.substr(aovPrefix.size());
      auto it = m_arbitraryAovs.find(token);
      if (it == m_arbitraryAovs.end()) {
        *width = *height = 0;
        *pixelType = ANARI_UNKNOWN;
        return nullptr;
      }
      *pixelType = it->second;
      const TfToken tok(token);
      const ANARIDataType pt = it->second;
      // The staging lookup (a map insert) runs on the worker too, so a map from
      // a completion callback cannot race an app thread's map (SPEC §8.7).
      std::vector<uint8_t> *staging = nullptr;
      state->worker->postWait([&]() {
        staging = &m_mappedAov[name];
        readbackAovBuffer(tok, pt, *staging);
      });
      return staging->data();
    }
  }

  // A standard AOV channel (SPEC §11.4): map only if it was enabled this frame.
  const AovChannelDesc *desc = nullptr;
  for (const AovChannelDesc &d : standardAovChannels()) {
    if (name == d.anariChannel) {
      desc = &d;
      break;
    }
  }
  if (!desc || !m_enabledAovChannels.count(name)) {
    *width = *height = 0;
    *pixelType = ANARI_UNKNOWN;
    return nullptr;
  }

  *pixelType = desc->pixelType;
  std::vector<uint8_t> *staging = nullptr;
  state->worker->postWait([&]() {
    staging = &m_mappedAov[name];
    readbackAov(*desc, *staging);
  });
  return staging->data();
}

void Frame::unmap(std::string_view /*channel*/)
{
  // Staging buffer is device-owned; nothing to release.
}

int Frame::frameReady(ANARIWaitMask m)
{
  // On the worker (a completion callback) WAIT cannot block on queued jobs —
  // they run after the current one — so report the current state instead.
  Worker *worker = deviceState()->worker;
  if (m == ANARI_WAIT && worker && !worker->onWorkerThread()) {
    // Flush the worker queue (FIFO) so the render job has completed.
    worker->postWait([]() {});
    return 1;
  }
  return m_rendered ? 1 : 0;
}

void Frame::discard()
{
  // v1 serializes renders; a discard simply lets the in-flight render finish.
}

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_DEFINITION(anari_hydra::Frame *);
}
