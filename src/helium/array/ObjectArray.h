// Copyright 2023-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Array1D.h"

namespace helium {

/*
 * Specialization of Array that holds ANARIObject handles (pointers to
 * BaseObject subclasses) rather than plain data. Maintains three parallel
 * handle lists: m_appHandles (all totalCapacity() handles from the
 * application, referenced), m_liveHandles (the handles in [begin, end)
 * followed by the appended ones; entries may be null), and m_appendedHandles
 * (extra handles added by the device after construction, e.g. to inject
 * implicit objects). The device iterates handlesBegin()/handlesEnd() to visit
 * the live handles. unmap() re-syncs the handle lists from the app's memory and
 * finalize() re-lays out m_liveHandles for the committed range; both notify
 * change observers so dependent objects are re-committed.
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
  // Copy (and reference) all totalCapacity() handles from the app's memory.
  void syncAppHandles();
  // Lay out m_liveHandles as [begin, end) of m_appHandles, then the appended
  // handles: exactly what handlesBegin()/handlesEnd() yield. A commit changes
  // the range in commitParameters() but lays it out in finalize(), so handles
  // are valid to read only after the array's finalize().
  void updateLiveHandles();

  std::vector<BaseObject *> m_appendedHandles;
  std::vector<BaseObject *> m_appHandles;
  mutable std::vector<BaseObject *> m_liveHandles;
  size_t m_capacity{0};
  size_t m_begin{0};
  size_t m_end{0};

 private:
  void privatize() override;
  bool privatizeCopiesAppData() const override;
};

} // namespace helium

HELIUM_ANARI_TYPEFOR_SPECIALIZATION(helium::ObjectArray *, ANARI_ARRAY1D);
