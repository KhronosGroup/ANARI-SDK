// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseDevice.h"

#include "HydraGlobalState.h"
#include "Object.h"

// std
#include <pxr/imaging/hd/renderDelegate.h>

#include <set>
#include <string>
#include <vector>

namespace anari_hydra {

// A standard AOV frame channel beyond color. Each maps an ANARI frame-channel
// name to the Hydra AOV token it reads back, the ANARI pixel type map()
// produces, and the KHR_FRAME_CHANNEL_* extension it claims. `channel.color` is
// unconditional and lives outside this table.
struct AovChannelDesc
{
  const char *anariChannel; // e.g. "channel.depth"
  pxr::TfToken hydraToken; // e.g. HdAovTokens->depth
  ANARIDataType pixelType; // e.g. ANARI_FLOAT32
  const char *extension; // e.g. "ANARI_KHR_FRAME_CHANNEL_DEPTH"
};

// The five standard AOV channels the bridge supports (depth, normal, and the
// object/primitive/instance IDs). Shared by the capability probe, the extension
// list, and the frame's readback.
const std::vector<AovChannelDesc> &standardAovChannels();

// Which Hydra AOV serves channel.normal on `delegate`: its world-space `normal`
// AOV when it publishes one (HdEmbree), else its eye-space `Neye` AOV (Storm;
// the frame rotates it to world space on readback), else an empty token
// (channel unavailable). Shared by the capability probe and the frame so both
// agree on the binding.
pxr::TfToken normalAovToken(pxr::HdRenderDelegate *delegate);

// Per-delegate extension truth, shared by the device (deviceGetProperty
// "extension") and library (getDeviceExtensions) paths.
//
// Read a delegate's published capability lists. MUST run on a worker thread
// (which owns the Hgi); a bare delegate instance suffices to read the type
// tokens — no render index or Hgi driver is needed. Also probes which standard
// AOV channels the delegate offers a valid default descriptor for, filling
// `aovChannels` with the ANARI channel names it supports. Also captures every
// HdRenderSettingDescriptor the delegate publishes into `renderSettings` for
// reflection as typed renderer parameters.
void readDelegateCaps(const pxr::TfToken &delegateId,
    pxr::TfTokenVector &rprims,
    pxr::TfTokenVector &sprims,
    pxr::TfTokenVector &bprims,
    std::set<std::string> &aovChannels,
    std::vector<RenderSettingDesc> &renderSettings);

// Build the honest ANARI extension list from probed capabilities. `outStrings`
// owns the storage; `outView` is a NUL-terminated const char* view into it.
void buildExtensionList(const pxr::TfTokenVector &rprims,
    const pxr::TfTokenVector &sprims,
    const pxr::TfTokenVector &bprims,
    const std::set<std::string> &aovChannels,
    std::vector<std::string> &outStrings,
    std::vector<const char *> &outView);

struct HydraDevice : public helium::BaseDevice
{
  // Data Arrays //////////////////////////////////////////////////////////////

  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) override;
  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) override;
  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) override;

  // API Objects //////////////////////////////////////////////////////////////

  ANARICamera newCamera(const char *type) override;
  ANARIFrame newFrame() override;
  ANARIGeometry newGeometry(const char *type) override;
  ANARIGroup newGroup() override;
  ANARIInstance newInstance(const char *type) override;
  ANARILight newLight(const char *type) override;
  ANARIMaterial newMaterial(const char *material_type) override;
  ANARIRenderer newRenderer(const char *type) override;
  ANARISampler newSampler(const char *type) override;
  ANARISpatialField newSpatialField(const char *type) override;
  ANARISurface newSurface() override;
  ANARIVolume newVolume(const char *type) override;
  ANARIWorld newWorld() override;

  // Query functions //////////////////////////////////////////////////////////

  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;

  const char **getObjectSubtypes(ANARIDataType objectType) override;
  const void *getObjectInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *infoName,
      ANARIDataType infoType) override;
  const void *getParameterInfo(ANARIDataType objectType,
      const char *objectSubtype,
      const char *parameterName,
      ANARIDataType parameterType,
      const char *infoName,
      ANARIDataType infoType) override;

  /////////////////////////////////////////////////////////////////////////////
  // Helper/other functions and data members
  /////////////////////////////////////////////////////////////////////////////

  HydraDevice(ANARIStatusCallback defaultCallback, const void *userPtr);
  HydraDevice(ANARILibrary, const char *subtype);
  ~HydraDevice() override;

  // Create the worker (and its GL context; see hydra/GLContext.h), then check
  // that the bound delegate can run with it. Idempotent. Returns false if the
  // device is unusable: a GL-only delegate (Storm) with no GL context, or an
  // ANARI_HYDRA_GL mode that is invalid or unavailable. The reason goes to
  // `whyNot` if given, else to the status callback. HydraLibrary::newDevice
  // calls this eagerly so such a device is never handed out.
  bool initDevice(std::string *whyNot = nullptr);

  // Introspection subtype gate; see the definition.
  bool resolveQuerySubtype(ANARIDataType objectType, const char *&subtype);

  // Probe the bound delegate's capability lists once, on the worker thread, and
  // cache them in device state. Idempotent; a no-op once probed.
  void probeCapabilities();

  void deviceCommitParameters() override;
  int deviceGetProperty(const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      uint32_t mask) override;

 private:
  HydraGlobalState *deviceState() const;
  void bindDelegate(const char *subtype);
  // Build + cache the reflected renderer parameter list into device state.
  void buildRendererParamList();

  bool m_initialized{false};
  bool m_usable{false};
};

} // namespace anari_hydra
