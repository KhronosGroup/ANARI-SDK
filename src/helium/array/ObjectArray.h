// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array1D.h"

namespace helium {

struct ObjectArray : public Array
{
  ObjectArray(BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d);
  ~ObjectArray();

  void commitParameters() override;
  void finalize() override;

  size_t totalSize() const override;
  size_t totalCapacity() const override;

  size_t size() const;

  void privatize() override;
  void unmap() override;

  BaseObject **handlesBegin() const;
  BaseObject **handlesEnd() const;

  void appendHandle(BaseObject *);
  void removeAppendedHandles();

 private:
  void updateInternalHandleArrays() const;

  mutable std::vector<BaseObject *> m_appendedHandles;
  mutable std::vector<BaseObject *> m_appHandles;
  mutable std::vector<BaseObject *> m_liveHandles;
  size_t m_capacity{0};
  size_t m_begin{0};
  size_t m_end{0};
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::ObjectArray *, ANARI_ARRAY1D);
