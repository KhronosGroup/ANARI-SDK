// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseObject.h"

namespace helium {

struct BaseArray : public BaseObject
{
  BaseArray(ANARIDataType type, BaseGlobalDeviceState *s);

  virtual void *map() = 0;
  virtual void unmap() = 0;
  virtual void privatize() = 0;

  void addCommitObserver(BaseObject *obj);
  void removeCommitObserver(BaseObject *obj);
  void notifyCommitObservers() const;

 protected:
  virtual void notifyObserver(BaseObject *) const = 0;

 private:
  std::vector<BaseObject *> m_observers;
};

} // namespace helium
