// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/detail/Device.h"

#include <algorithm>
#include <stdexcept>

namespace anari {

// extension functions ////////////////////////////////////////////////////////

ANARIObject Device::newObject(const char *objectType, const char *type)
{
  return nullptr;
}

void (*Device::getProcAddress(const char *name))(void)
{
  return nullptr;
}

// Device definitions /////////////////////////////////////////////////////////

ANARIDevice Device::this_device() const
{
  return (ANARIDevice)this;
}

ANARIStatusCallback Device::defaultStatusCallback() const
{
  return m_defaultStatusCB;
}

void *Device::defaultStatusCallbackUserPtr() const
{
  return m_defaultStatusCBUserPtr;
}

ANARI_TYPEFOR_DEFINITION(Device *);

} // namespace anari
