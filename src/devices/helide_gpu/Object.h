// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HelideGPUDeviceGlobalState.h"
#include "HelideGPUMath.h"
// helium
#include "helium/BaseObject.h"
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <functional>
#include <future>
#include <string_view>

namespace helide_gpu {

struct Object : public helium::BaseObject
{
  Object(ANARIDataType type, HelideGPUDeviceGlobalState *s);
  virtual ~Object() = default;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  virtual void commitParameters() override;
  virtual void finalize() override;

  bool isValid() const override;

  HelideGPUDeviceGlobalState *deviceState() const;
};

// This gets instantiated for all object subtypes which are not known
struct UnknownObject : public Object
{
  UnknownObject(ANARIDataType type, HelideGPUDeviceGlobalState *s);
  ~UnknownObject() override = default;
  bool isValid() const override;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename OBJECT_T, typename METHOD_T>
inline helium::tasking::Future gpu_enqueue_method(OBJECT_T *o, METHOD_T m)
{
  auto &state = *o->deviceState();
  if (state.gpu.thread.onWorkerThread()) {
    // Already on the GPU thread (e.g. finalize() during a commitBuffer flush
    // inside gpu_renderFrame): run now, in order, so GPU resources exist before
    // the render pass. TaskQueue::enqueue no longer runs worker-thread work
    // inline (that FIFO change is required by the forward device), so this
    // synchronous path must live here.
    std::packaged_task<void()> t(std::bind(m, o));
    auto f = t.get_future();
    t();
    return f;
  }
  return state.gpu.thread.enqueue(m, o);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Object *, ANARI_OBJECT);
