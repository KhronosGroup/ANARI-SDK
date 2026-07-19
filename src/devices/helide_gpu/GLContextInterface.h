// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari.h>

namespace helide_gpu {

// Status reporting helper used by HelideGPUDevice and GL/GPU context code.
void anariDeviceReportStatus(ANARIDevice,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *format,
    ...);

} // namespace helide_gpu
