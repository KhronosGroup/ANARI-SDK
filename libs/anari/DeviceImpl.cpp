// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/backend/DeviceImpl.h"

#include <algorithm>
#include <stdexcept>

namespace anari {

// extension functions ////////////////////////////////////////////////////////

ANARIObject DeviceImpl::newObject(const char *objectType, const char *type)
{
  (void)objectType;
  (void)type;
  return nullptr;
}

void (*DeviceImpl::getProcAddress(const char *name))(void)
{
  (void)name;
  return nullptr;
}

// Device definitions /////////////////////////////////////////////////////////

DeviceImpl::DeviceImpl(ANARILibrary library)
    : m_defaultStatusCB(((LibraryImpl *)library)->defaultStatusCB()),
      m_defaultStatusCBUserPtr(
          ((LibraryImpl *)library)->defaultStatusCBUserPtr())
{}

ANARIDevice DeviceImpl::this_device() const
{
  return (ANARIDevice)this;
}

ANARIStatusCallback DeviceImpl::defaultStatusCallback() const
{
  return m_defaultStatusCB;
}

const void *DeviceImpl::defaultStatusCallbackUserPtr() const
{
  return m_defaultStatusCBUserPtr;
}

bool DeviceImpl::handleIsDevice(ANARIObject obj) const
{
  return static_cast<const void *>(obj) == static_cast<const void *>(this);
}

ANARI_TYPEFOR_DEFINITION(DeviceImpl *);

} // namespace anari
