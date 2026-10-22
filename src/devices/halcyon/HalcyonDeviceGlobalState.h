// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HalcyonMath.h"
#include "renderer/pathtracer/ImageFile.h"
#include "utility/IndexedVector.hpp"
#include "utility/IspcObjectStorage.hpp"
#include "utility/RenderingSemaphore.hpp"
// helium
#include "helium/BaseGlobalDeviceState.h"
#include "helium/TaskQueue.h"
// embree
#include <embree4/rtcore.h>
// std
#include <mutex>

namespace halcyon {

struct HalcyonDeviceGlobalState : public helium::BaseGlobalDeviceState
{
  anari::Device anariDevice{nullptr};
  RTCDevice embreeDevice{nullptr};

  struct ObjectUpdates
  {
    helium::TimeStamp lastBLSReconstructSceneRequest{0};
    helium::TimeStamp lastBLSCommitSceneRequest{0};
    helium::TimeStamp lastTLSReconstructSceneRequest{0};
  } objectUpdates;

  struct DeviceParameters
  {
    bool allowInvalidSurfaceMaterials{true};
    float4 invalidMaterialColor{1.f, 0.f, 1.f, 1.f};
  };

  // Device parameters in effect. Only the task queue's worker reads or writes
  // them (applyDeviceParameters() at the start of a render), so they never
  // change during a render.
  bool allowInvalidSurfaceMaterials{true};
  float4 invalidMaterialColor{1.f, 0.f, 1.f, 1.f};
  // When device parameters were last applied; frames render again after it,
  // like after an object finalization. Worker only.
  helium::TimeStamp lastDeviceParametersApplied{0};

  // Stores device parameters committed on an application thread; the next
  // render applies them.
  void commitDeviceParameters(const DeviceParameters &p);
  // Called by renders on the worker: puts committed device parameters into
  // effect, requesting a group rebuild if surface validity changed.
  void applyDeviceParameters();

  // Flushes the commit buffer (on the worker, in device work), again while
  // objects that a flush finalized notified their observers: helium stages
  // those for the next flush, and the render or query at hand must see them
  // (a 'materialx' material's surfaces, which see its validity, say)
  void flushCommitBuffer();

  // Runs commit buffer flushes and frame renders in submission order
  helium::tasking::TaskQueue taskQueue{64};

  // Keeps the task queue's work from reading arrays the app has mapped
  RenderingSemaphore renderingSemaphore;

  // The image files materials read ('materialx' documents' textures),
  // shared between them
  pathtracer::ImageFileCache imageFiles;

  struct ISPCObjectRegistry
  {
    IndexedVector<IspcObjectStorage<160>> frames;
    IndexedVector<IspcObjectStorage<160>> renderers;
    IndexedVector<IspcObjectStorage<144>> cameras;
    IndexedVector<IspcObjectStorage<40>> worlds;
    IndexedVector<IspcObjectStorage<88>> instances;
    IndexedVector<IspcObjectStorage<32>> groups;
    IndexedVector<IspcObjectStorage<24>> surfaces;
    IndexedVector<IspcObjectStorage<560>> geometries;
    IndexedVector<IspcObjectStorage<96>> materials;
    IndexedVector<IspcObjectStorage<256>> samplers;
    IndexedVector<IspcObjectStorage<176>> spatialFields;
    IndexedVector<IspcObjectStorage<112>> volumes;
  } registry;

  HalcyonDeviceGlobalState(ANARIDevice d);

 private:
  std::mutex m_deviceParametersMutex;
  DeviceParameters m_committedDeviceParameters;
  bool m_deviceParametersCommitted{false};
};

// Helper functions/macros ////////////////////////////////////////////////////

#define HALCYON_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                 \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define HALCYON_ANARI_TYPEFOR_DEFINITION(type)                                 \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace halcyon
