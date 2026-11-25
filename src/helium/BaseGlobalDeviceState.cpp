// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseGlobalDeviceState.h"
#include "BaseDevice.h"

namespace helium {

BaseGlobalDeviceState::BaseGlobalDeviceState(ANARIDevice d) : m_device(d)
{
  messageFunction = [&, d](ANARIStatusSeverity severity,
                        const std::string &msg,
                        anari::DataType objType,
                        const void *obj) {
    if (!statusCB)
      return;
    statusCB(statusCBUserPtr,
        d,
        (ANARIObject)obj,
        objType,
        severity,
        severity <= ANARI_SEVERITY_WARNING ? ANARI_STATUS_NO_ERROR
                                           : ANARI_STATUS_UNKNOWN_ERROR,
        msg.c_str());
  };
}

void BaseGlobalDeviceState::runDeviceRelease(const std::function<void()> &work)
{
  if (m_device == nullptr) {
    work();
    return;
  }
  // this_device() is the BaseDevice's DeviceImpl address
  static_cast<BaseDevice *>((anari::DeviceImpl *)m_device)
      ->runDeviceRelease(work);
}

} // namespace helium
