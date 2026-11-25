## Copyright 2026 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

# OpenQMC (header-only), fetched at configure time: the sampler library
# Typhoon samples with (renderer/sampling/sampling.h), in the version Typhoon's
# build_usd.py installs. halcyon_openqmc_data is the directory of OpenQMC's
# data tables, which the pathtracer's sample generator compiles in
# (renderer/pathtracer/SobolBnTables.cpp): the device's only use of OpenQMC;
# its sampler code is ported to ISPC (renderer/pathtracer/Sampling.isph).

anari_sdk_fetch_project(
  NAME halcyon_openqmc
  URL "https://github.com/AcademySoftwareFoundation/openqmc/archive/refs/tags/v0.7.1.zip"
  MD5 2fd4122792f7a1726c9fc3aa3c38cbdf
)

add_library(halcyon_openqmc_data INTERFACE)
target_include_directories(halcyon_openqmc_data SYSTEM INTERFACE
  ${halcyon_openqmc_LOCATION}/include/oqmc/data)
