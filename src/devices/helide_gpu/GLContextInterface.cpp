// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "GLContextInterface.h"
#include "HelideGPUDevice.h"
// std
#include <cstdarg>
#include <vector>

namespace helide_gpu {

void anariDeviceReportStatus(ANARIDevice handle,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *format,
    ...)
{
  if (handle == nullptr)
    return;

  auto *d = (HelideGPUDevice *)handle;
  auto &state = *d->deviceState();
  if (!state.statusCB)
    return;

  va_list arglist;
  va_list arglist_copy;
  va_start(arglist, format);
  va_copy(arglist_copy, arglist);
  int count = std::vsnprintf(nullptr, 0, format, arglist);
  va_end(arglist);

  std::vector<char> formattedMessage(size_t(count + 1));

  std::vsnprintf(
      formattedMessage.data(), size_t(count + 1), format, arglist_copy);
  va_end(arglist_copy);

  state.statusCB(state.statusCBUserPtr,
      handle,
      handle,
      ANARI_DEVICE,
      severity,
      code,
      formattedMessage.data());
}

} // namespace helide_gpu
