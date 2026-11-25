// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "HydraGlobalState.h"

namespace anari_hydra {

HydraGlobalState::HydraGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{}

HydraGlobalState::~HydraGlobalState() = default;

} // namespace anari_hydra
