// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// OpenQMC's blue-noise key and rank tables for its Sobol sampler
// (oqmc::bntables::sobol), which the pathtracer's sample generator
// (Sampling.isph) reads. The data is OpenQMC's own (Apache-2.0, see
// THIRD-PARTY.md), included from the OpenQMC sources fetched at configure
// time (external/openqmc).

#include <cstdint>

extern "C" {

extern const uint32_t halcyon_sobolBnKeyTable[65536];
extern const uint32_t halcyon_sobolBnRankTable[65536];

const uint32_t halcyon_sobolBnKeyTable[65536] = {
#include "sobol/keys.txt"
};

const uint32_t halcyon_sobolBnRankTable[65536] = {
#include "sobol/ranks.txt"
};

} // extern "C"
