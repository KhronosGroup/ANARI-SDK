// Copyright 2022-2024 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "BaseGlobalDeviceState.h"

namespace helium {

BaseGlobalDeviceState::BaseGlobalDeviceState(ANARIDevice d)
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

} // namespace helium
