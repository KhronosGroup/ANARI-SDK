// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseArray.h"
// std
#include <algorithm>

namespace helium {

BaseArray::BaseArray(ANARIDataType type, BaseGlobalDeviceState *s)
    : BaseObject(type, s)
{}

void BaseArray::addCommitObserver(BaseObject *obj)
{
  m_observers.push_back(obj);
}

void BaseArray::removeCommitObserver(BaseObject *obj)
{
  m_observers.erase(std::remove_if(m_observers.begin(),
                        m_observers.end(),
                        [&](BaseObject *o) -> bool { return o == obj; }),
      m_observers.end());
}

void BaseArray::notifyCommitObservers() const
{
  for (auto o : m_observers)
    notifyObserver(o);
}

} // namespace helium
