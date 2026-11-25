// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0

// The vendored tables native physicallyBased (PathPbmLobes.isph) reads, in
// place, so that the native lobes and the C++ ones compensate from the same
// data: MaterialXCpp's directional GGX missing-energy table
// (materials/bsdf/ggxEnergyLut.h) and its BSDL reflection-only dielectric
// layer filter (materials/bsdf/dielectricReflFrontLut.h, generated with
// OpenShadingLanguage's libbsdl, BSD-3-Clause; see THIRD-PARTY.md)

// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectricReflFrontLut.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/energyCompensation.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/ggxEnergyLut.h>

namespace {

namespace lut = mxcpp::Bsdf::detail;
namespace bsdl = mxcpp::bsdf_luts;

static_assert(
    lut::kGgxEnergyCosThetaCount == 16 && lut::kGgxEnergyAlphaCount == 16,
    "PathPbmLobes.isph's PATH_PBM_GGX_ENERGY_SIZE is the table's size");
static_assert(sizeof(lut::kGgxMissingEnergy)
        == sizeof(float) * lut::kGgxEnergyAlphaCount
            * lut::kGgxEnergyCosThetaCount,
    "the table is read as contiguous rows of alpha");
static_assert(bsdl::kBsdlDielectricReflFrontIorCount == 32
        && bsdl::kBsdlDielectricReflFrontRoughnessCount == 16
        && bsdl::kBsdlDielectricReflFrontCosThetaCount == 16,
    "PathPbmLobes.isph's PATH_PBM_REFL_FRONT_* are the table's sizes");
static_assert(sizeof(bsdl::kBsdlDielectricReflFrontFilter)
        == sizeof(float) * bsdl::kBsdlDielectricReflFrontIorCount
            * bsdl::kBsdlDielectricReflFrontRoughnessCount
            * bsdl::kBsdlDielectricReflFrontCosThetaCount,
    "the table is read as contiguous ior, roughness, cosine");
static_assert(
    lut::kBsdlDielectricIorMin == 1.001f && lut::kBsdlDielectricIorMax == 5.0f,
    "PathPbmLobes.isph's PATH_PBM_BSDL_IOR_* are the tables' IOR domain");

} // namespace

extern "C" {
// PathPbmLobes.isph's PathPbm_ggxEnergyCosThetaTable,
// PathPbm_ggxMissingEnergyTable and PathPbm_dielectricReflFrontTable
extern const float *const PathPbm_ggxEnergyCosThetaTable;
extern const float *const PathPbm_ggxMissingEnergyTable;
extern const float *const PathPbm_dielectricReflFrontTable;
}

const float *const PathPbm_ggxEnergyCosThetaTable = lut::kGgxEnergyCosTheta;
const float *const PathPbm_ggxMissingEnergyTable =
    &lut::kGgxMissingEnergy[0][0];
const float *const PathPbm_dielectricReflFrontTable =
    &bsdl::kBsdlDielectricReflFrontFilter[0][0][0];
