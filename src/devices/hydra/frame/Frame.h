// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "camera/Camera.h"
#include "renderer/Renderer.h"
#include "world/World.h"
// helium
#include "helium/BaseFrame.h"
// pxr
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hdx/taskControllerSceneIndex.h>
// std
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace anari_hydra {

// Each ANARI frame owns one render index, task controller, and delegate
// instance (SPEC §8.4); the device-owned Hgi outlives all frames. renderFrame
// posts a worker job: render prologue (stage-2 drain) -> Sync/Execute -> poll.
struct Frame : public helium::BaseFrame
{
  Frame(HydraGlobalState *s);
  ~Frame() override;

  bool isValid() const override;
  HydraGlobalState *deviceState() const;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  void renderFrame() override;
  void *map(std::string_view channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;
  void unmap(std::string_view channel) override;
  int frameReady(ANARIWaitMask m) override;
  void discard() override;

 private:
  // Worker-thread only:
  void ensureHydra(); // create delegate + render index + task controller
  void renderOnWorker(); // prologue drain + Execute loop
  // Mark the frame ready and fire the completion callback (SPEC §8.7). Runs
  // once per render job, after renderOnWorker, whether or not it succeeded.
  void completeOnWorker();
  void teardownHydra(); // strict teardown order (SPEC §9.6)
  std::map<int, PickingIds> m_pickingIds;
  void readbackPicking(const pxr::TfToken &token, std::vector<uint8_t> &out);
  void readbackColor(); // color AOV -> m_mappedColor (SPEC §8.5)
  void readbackAov(const struct AovChannelDesc &desc,
      std::vector<uint8_t> &out); // standard AOV -> staging (SPEC §11.4)
  // channel.normal on a delegate without a `normal` AOV (Storm): read the
  // eye-space Neye target and rotate it into world space (SPEC §11.4).
  void readbackNormalFromNeye(std::vector<uint8_t> &out);
  // Hydra AOV token the frame binds for a standard channel: the descriptor's
  // token, except normal -> m_normalToken (normalAovToken of the delegate).
  pxr::TfToken boundAovToken(const struct AovChannelDesc &desc) const;
  // Read a Hydra AOV bprim back into `out`, converting to `pixelType` (SPEC
  // §11.4). Shared by the standard-channel and arbitrary-named AOV paths.
  void readbackAovBuffer(const pxr::TfToken &hydraToken,
      ANARIDataType pixelType,
      std::vector<uint8_t> &out);

  helium::IntrusivePtr<World> m_world;
  helium::IntrusivePtr<Camera> m_camera;
  helium::IntrusivePtr<Renderer> m_renderer;

  uint32_t m_width{0};
  uint32_t m_height{0};
  ANARIDataType m_colorType{ANARI_UFIXED8_RGBA_SRGB};

  // Standard AOV channels beyond color the app enabled (SPEC §11.4), by ANARI
  // channel name (e.g. "channel.depth"). Set in commitParameters; drives which
  // Hydra AOV bprims the task controller wires and which map() can read back.
  std::set<std::string> m_enabledAovChannels;

  // Arbitrary named Hydra AOVs the app requested via `channel.aov:<hydraToken>`
  // frame parameters (SPEC §11.4, ANARI_HYDRA_FRAME_AOV), keyed by the Hydra
  // token (the suffix) with the value the requested ANARIDataType. Set in
  // commitParameters; drives the extra AOV bprims, their honored buffer format
  // (SetRenderOutputSettings), and which map() can read back.
  std::map<std::string, ANARIDataType> m_arbitraryAovs;

  // Hydra objects, created lazily on the worker thread.
  pxr::HdPluginRenderDelegateUniqueHandle m_renderDelegate;
  std::unique_ptr<pxr::HdRenderIndex> m_renderIndex;
  pxr::HdxTaskControllerSceneIndexRefPtr m_taskController;
  pxr::HdEngine m_engine;
  bool m_hydraReady{false};
  // Hydra AOV serving channel.normal on this delegate (normalAovToken, set in
  // ensureHydra): `normal` (world space) or `Neye` (eye space, rotated on
  // read).
  pxr::TfToken m_normalToken;
  pxr::HdSceneIndexBaseRefPtr m_insertedWorldScene;
  uint64_t m_mirrorRevision{0};
  uint64_t m_insertedWorldSerial{0}; // which world's mirror is inserted

  // A small frame-owned retained scene index holding at most one synthesized
  // uniform dome light at /anari/frame/ambient — the renderer's
  // ambientRadiance/ ambientColor fill (SPEC §4.7/§6.6). Frame-owned, NOT the
  // world mirror: ambient is a renderer/frame concern, and the mirror is shared
  // across frames. Inserted alongside the mirror in ensureHydra;
  // authored/dirtied/removed as ambientRadiance crosses 0 or its
  // color/intensity change.
  pxr::HdRetainedSceneIndexRefPtr m_ambientScene;
  bool m_ambientAuthored{false}; // a dome sprim is currently present
  float m_ambientAuthoredRadiance{0.f};
  pxr::GfVec3f m_ambientAuthoredColor{1.f, 1.f, 1.f};
  // The dome's registry image (ambientColor * ambientRadiance), re-pathed on
  // every change; the serial keeps frames' images apart.
  std::string m_ambientTexPath;
  uint64_t m_ambientFrameSerial{0};
  uint64_t m_ambientRevision{0};
  uint32_t m_configuredWidth{0};
  uint32_t m_configuredHeight{0};
  // Which AOV channel set is currently wired into SetRenderOutputs; guards the
  // (bprim-rewiring) SetRenderOutputs call to only fire when the set changes.
  std::set<std::string> m_configuredAovChannels;
  // Which arbitrary-AOV set is currently wired (token -> requested type); part
  // of the SetRenderOutputs guard key alongside m_configuredAovChannels.
  std::map<std::string, ANARIDataType> m_configuredArbitraryAovs;
  // Projection used for the rendered depth buffer, preserved until next render.
  bool m_orthographic{false};
  pxr::GfMatrix4d m_inverseProjection{1.0};
  pxr::GfMatrix4d m_viewMatrix{1.0};
  std::vector<uint32_t> m_stormPrimitiveIds;
  std::vector<uint32_t> m_stormObjectIds;
  std::vector<uint32_t> m_stormInstanceIds;
  void renderPicking();
  bool m_outputsSet{false}; // SetRenderOutputs called at least once

  // The renderer's background image (KHR_RENDERER_BACKGROUND_IMAGE, SPEC
  // §4.7) snapshotted for the last render, or null for a constant background.
  // Worker-thread only; readbackColor composites it under the color AOV, so a
  // renderer commit after the render does not change an already rendered frame.
  std::shared_ptr<const AnariImageData> m_backgroundImage;

  // Readback staging for map() (converted to the requested ANARI format).
  std::vector<uint8_t> m_mappedColor;
  // Per-channel readback staging for the standard AOV channels (SPEC §11.4),
  // keyed by ANARI channel name. Device-owned; kept alive across unmap.
  std::map<std::string, std::vector<uint8_t>> m_mappedAov;

  std::atomic<bool> m_rendered{false};

  // KHR_FRAME_COMPLETION_CALLBACK (SPEC §8.7). Written by commitParameters
  // (normally on the worker's stage-1 flush, but a WAIT property query can
  // flush on an app thread) and read by completeOnWorker, hence the mutex.
  std::mutex m_callbackMutex;
  ANARIFrameCompletionCallback m_callback{nullptr};
  const void *m_callbackUserData{nullptr};
  // Wall-clock seconds the last completed frame render took, measured on the
  // worker thread in renderOnWorker and read on the app thread in getProperty
  // (the "duration" property, SPEC §8.2). Atomic for the same cross-thread
  // reason as m_rendered above.
  std::atomic<float> m_duration{0.f};
  // Storm's GPU texture bytes after the last completed render (the
  // "hydra.textureMemory" property), stamped and read like m_duration.
  std::atomic<uint64_t> m_textureMemory{0};
  std::atomic<bool> m_hasTextureMemory{false};
};

} // namespace anari_hydra

namespace anari {
ANARI_TYPEFOR_SPECIALIZATION(anari_hydra::Frame *, ANARI_FRAME);
}
