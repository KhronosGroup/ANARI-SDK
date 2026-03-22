// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array1D.h"

namespace helium {

/*
 * Specialization of Array that holds ANARIObject handles (pointers to
 * BaseObject subclasses) rather than plain data. Maintains three parallel
 * handle lists: m_appHandles (raw pointers from the application), m_liveHandles
 * (only the non-null, valid entries used during traversal), and
 * m_appendedHandles (extra handles added by the device after construction, e.g.
 * to inject implicit objects). The device iterates handlesBegin()/handlesEnd()
 * to visit live objects. unmap() re-syncs internal lists and notifies change
 * observers so dependent objects are re-committed.
 */
struct ObjectArray : public Array
{
  ObjectArray(BaseGlobalDeviceState *state, const Array1DMemoryDescriptor &d);
  ~ObjectArray();

  void commitParameters() override;
  void finalize() override;

  size_t totalSize() const override;
  size_t totalCapacity() const override;

  size_t size() const;

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

 private:
  void privatize() override;
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::ObjectArray *, ANARI_ARRAY1D);
