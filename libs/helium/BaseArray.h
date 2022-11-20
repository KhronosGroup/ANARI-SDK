// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseObject.h"

namespace helium {

struct BaseArray : public BaseObject
{
  BaseArray(ANARIDataType type, BaseGlobalDeviceState *s);

  // Implement anariMapArray()
  virtual void *map() = 0;

  // Implement anariUnmapArray()
  virtual void unmap() = 0;

  // This is invoked when this object's public ref count is 0, but still has a
  // non-zero internal ref count. See README for additional explanation.
  virtual void privatize() = 0;

  void addCommitObserver(BaseObject *obj);
  void removeCommitObserver(BaseObject *obj);
  void notifyCommitObservers() const;

 protected:
  // Handle what happens when the observing object 'obj' is being notified of
  // that this array has changed.
  virtual void notifyObserver(BaseObject *obj) const = 0;

 private:
  std::vector<BaseObject *> m_observers;
};

} // namespace helium
