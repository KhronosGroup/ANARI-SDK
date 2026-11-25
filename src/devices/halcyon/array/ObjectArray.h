// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/ObjectArray.h"

namespace halcyon {

// Unlike the data arrays (Array1D.h), object arrays don't need to notify their
// observers when helium privatizes them: helium keeps the handles in its own
// vectors from construction and unmap on, and users read those, never the app
// memory.

using ObjectArray = helium::ObjectArray;

} // namespace halcyon
