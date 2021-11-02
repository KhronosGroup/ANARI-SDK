// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/detail/Device.h"

#include <algorithm>
#include <stdexcept>

namespace anari {

// Static data ////////////////////////////////////////////////////////////////

static FactoryMap<Device> g_devicesMap;
static FactoryVector<Device, Device *> g_layerVector;

// extension functions ////////////////////////////////////////////////////////

ANARI_INTERFACE ANARIObject Device::newObject(
    const char *objectType, const char *type)
{
  return nullptr;
}

ANARI_INTERFACE void (*Device::getProcAddress(const char *name))(void)
{
  return nullptr;
}

// Helper functions ///////////////////////////////////////////////////////////

template <typename ANARI_CLASS>
inline ANARI_CLASS *createInstanceHelper(
    const std::string &type, FactoryFcn<ANARI_CLASS> fcn)
{
  if (fcn) {
    auto *obj = fcn();
    if (obj != nullptr)
      return obj;
  }

  throw std::runtime_error("Could not find object of type: " + type
      + ".  Make sure you have the correct modules linked and initialized.");
}

// Device definitions /////////////////////////////////////////////////////////

Device *Device::createDevice(const char *type,
    ANARIStatusCallback defaultStatusCB,
    void *defaultStatusCBUserPtr)
{
  Device *top = createInstanceHelper(type, g_devicesMap[type]);
  for (auto f : g_layerVector)
    top = f(top);

  top->m_defaultStatusCB = defaultStatusCB;
  top->m_defaultStatusCBUserPtr = defaultStatusCBUserPtr;

  return top;
}

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

void Device::registerType(const char *type, FactoryFcn<Device> f)
{
  g_devicesMap[type] = f;
}

void Device::registerLayer(FactoryFcn<Device, Device *> f)
{
  auto pos = std::find(std::begin(g_layerVector), std::end(g_layerVector), f);
  if (pos == std::end(g_layerVector)) {
    g_layerVector.push_back(f);
  }
}

ANARI_TYPEFOR_DEFINITION(Device *);

} // namespace anari
