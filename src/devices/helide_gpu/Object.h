// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "HelideGPUDeviceGlobalState.h"
#include "HelideGPUMath.h"
// helium
#include "helium/BaseObject.h"
#include "helium/utility/ChangeObserverPtr.h"
// std
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
  return state.gpu.thread.enqueue(m, o);
}

} // namespace helide_gpu

HELIDE_GPU_ANARI_TYPEFOR_SPECIALIZATION(helide_gpu::Object *, ANARI_OBJECT);
