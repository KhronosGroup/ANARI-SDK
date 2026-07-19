// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace helium {

// Monotonically increasing counter used to order events across objects without
// storing absolute wall-clock times. Each call to newTimeStamp() returns a
// value strictly greater than all previous values. BaseObject stores four
// TimeStamps (lastParameterChanged, lastUpdated, lastCommitted, lastFinalized)
// so the commit buffer can skip redundant work by comparing them.
using TimeStamp = uint64_t;
TimeStamp newTimeStamp();

} // namespace helium