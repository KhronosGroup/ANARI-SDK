// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CaptureObject.h"
// std
#include <string>
#include <vector>

namespace capture_device {

// What is wrong with an object, as the spec's definitions describe the object
// type: an unknown subtype, unknown parameters, parameters of the wrong type.
// 'id' names the object in each message. Renderers have no checks, as each
// device defines its own.
std::vector<std::string> validateObject(const std::string &id,
    ANARIDataType type,
    const std::string &subtype,
    const CommittedParams::List &params);

} // namespace capture_device
