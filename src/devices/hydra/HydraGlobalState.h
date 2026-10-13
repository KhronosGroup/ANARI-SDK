// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// pxr
#include <pxr/base/tf/token.h>
// std
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace anari_hydra {

class Worker;
struct Object;
struct Frame;

// One reflected delegate render setting. Captured at probe time from an
// HdRenderSettingDescriptor: the verbatim Hydra token, the descriptor's human
// name, the ANARIDataType its default VtValue maps to, and the default value
// serialized to ANARI-typed bytes (empty when the type is unmapped or STRING,
// where `defaultString` carries the text). Storage is stable for the device
// lifetime so reflected const char* / value pointers stay valid.
struct RenderSettingDesc
{
  std::string token; // verbatim Hydra render-setting token
  std::string humanName; // HdRenderSettingDescriptor::name
  ANARIDataType type{ANARI_UNKNOWN};
  std::vector<uint8_t> defaultBytes; // ANARI-typed default value bytes
  std::string defaultString; // default text when type == ANARI_STRING
};

// The mirror-dirty set: the sole interface between device-side commit
// application (stage 1, finalize()) and mirror authoring (stage 2, the render
// prologue). finalize() pushes marks here; the render prologue drains them into
// locator-precise scene-index calls. Mirror-mutation code lives nowhere else.
struct MirrorDirtySet
{
  void mark(Object *o)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirty.insert(o);
  }

  // Drain the set into `out` and clear. Called only by the render prologue on
  // the worker thread.
  void drain(std::unordered_set<Object *> &out)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    out = std::move(m_dirty);
    m_dirty.clear();
  }

  bool empty() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dirty.empty();
  }

 private:
  mutable std::mutex m_mutex;
  std::unordered_set<Object *> m_dirty;
};

struct HydraGlobalState : public helium::BaseGlobalDeviceState
{
  HydraGlobalState(ANARIDevice d);
  ~HydraGlobalState() override;

  // The single Hydra render delegate this device is bound to for its whole
  // life. Resolved from the device subtype at creation.
  std::string delegateSubtype;
  pxr::TfToken delegateId;
  // The delegate's display name (e.g. "GL" for Storm), used to fetch its
  // registered scene-index filter chain (material processing).
  std::string sceneIndexPluginName;

  // Per-delegate capability truth. The bound delegate publishes what it can
  // render via GetSupported{R,S,B}primTypes; the device probes these once on
  // the worker thread (which owns the Hgi) and gates its reported ANARI
  // extension / object-subtype lists on them (no volumes on HdEmbree, etc.).
  pxr::TfTokenVector supportedRprims;
  pxr::TfTokenVector supportedSprims;
  pxr::TfTokenVector supportedBprims;
  // Which standard AOV frame channels the delegate offers a valid default
  // descriptor for; gates the KHR_FRAME_CHANNEL_* extensions. Keyed by ANARI
  // channel name (e.g. "channel.depth").
  std::set<std::string> supportedAovChannels;
  // Every HdRenderSettingDescriptor the bound delegate publishes, captured at
  // probe time and reflected as a typed ANARI renderer parameter.
  std::vector<RenderSettingDesc> renderSettingDescs;
  bool capabilitiesProbed{false};

  // Cached C-API views built lazily from the capability sets. The std::string
  // storage keeps the const char* pointers alive for the device's lifetime; the
  // subtype views point at static string literals (always valid).
  // NUL-terminated.
  std::vector<std::string> extensionStrings;
  std::vector<const char *> extensionView;
  std::vector<const char *> geometrySubtypeView;
  std::vector<const char *> samplerSubtypeView;
  std::vector<const char *> lightSubtypeView;
  // Cached NUL-terminated (name==nullptr) reflected renderer parameter list
  // returned by
  // getObjectInfo(ANARI_RENDERER,...,"parameter",ANARI_PARAMETER_LIST). Names
  // point at stable string storage (the generated introspection table for the
  // bridge's own parameters; renderSettingDescs[i].token for verbatim delegate
  // settings).
  std::vector<ANARIParameter> rendererParamView;
  // Per-delegate extension-gated views over the generated introspection
  // tables (Introspection.h): parameter lists keyed (type, subtype), the frame
  // channel list, and stable int slots for INT32 sourceExtension answers.
  std::map<std::pair<ANARIDataType, std::string>, std::vector<ANARIParameter>>
      reflectedParamLists;
  std::vector<const char *> frameChannelView;
  std::vector<int32_t> extensionIndices;

  // The device worker thread; owns the Hgi and all Hydra work.
  Worker *worker{nullptr};

  // Device-assigned monotonic serials for the prim-path taxonomy.
  uint64_t nextWorldSerial{1};
  uint64_t nextSerial{1}; // shared across surfaces/materials/lights/...

  MirrorDirtySet mirrorDirty;
  // Worker-owned epoch: draining marks in one frame must not hide updates
  // from other frames/world mirrors.
  uint64_t mirrorRevision{0};

  anari::Device anariDevice{nullptr};

  // Frame completion callbacks. `releasing` is set once anariRelease(device)
  // begins: renders requested from then on (e.g. a callback's re-render chain)
  // are dropped so the release drain terminates.
  std::atomic<bool> releasing{false};
};

inline HydraGlobalState *asHydraState(helium::BaseGlobalDeviceState *s)
{
  return static_cast<HydraGlobalState *>(s);
}

} // namespace anari_hydra
