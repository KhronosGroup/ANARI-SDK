// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/detail/Device.h"

#include <algorithm>
#include <stdexcept>

namespace anari {

// extension functions ////////////////////////////////////////////////////////

ANARIObject DeviceImpl::newObject(const char *objectType, const char *type)
{
  return nullptr;
}

void (*DeviceImpl::getProcAddress(const char *name))(void)
{
  return nullptr;
}

// Device definitions /////////////////////////////////////////////////////////

ANARIDevice DeviceImpl::this_device() const
{
  return (ANARIDevice)this;
}

ANARIStatusCallback DeviceImpl::defaultStatusCallback() const
{
  return m_defaultStatusCB;
}

void *DeviceImpl::defaultStatusCallbackUserPtr() const
{
  return m_defaultStatusCBUserPtr;
}

ANARI_TYPEFOR_DEFINITION(DeviceImpl *);

} // namespace anari
