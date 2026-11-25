//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#define BSDL_UNROLL()

#include <BSDL/config.h>

#include <BSDL/MTX/bsdf_dielectric_decl.h>
#include <BSDL/microfacet_tools_impl.h>
#include <BSDL/MTX/bsdf_dielectric_impl.h>

#undef BSDL_UNROLL

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <thread>
#include <vector>

namespace {

constexpr int kCosThetaCount = 16;
constexpr int kRoughnessCount = 16;
constexpr int kIorCount = 32;
constexpr int kSampleSide = 128;
constexpr int kSampleCount = kSampleSide * kSampleSide;
constexpr int kValueCount = kCosThetaCount * kRoughnessCount * kIorCount;

uint32_t
_ReverseBitsLowDiscrepancy(uint32_t i)
{
    uint32_t result = 0;
    for (uint32_t v = 1U << 31; i; i >>= 1, v |= v >> 1) {
        if (i & 1) {
            result ^= v;
        }
    }
    return result;
}

uint32_t
_InverseReverseBitsLowDiscrepancy(uint32_t i)
{
    uint32_t result = 0;
    for (uint32_t v = 3U << 30; i; i >>= 1, v >>= 1) {
        if (i & 1) {
            result ^= v;
        }
    }
    return result;
}

Imath::V3f
_GetSample(int sampleIdx, uint32_t scrambleX, uint32_t scrambleY,
           uint32_t scrambleZ)
{
    const uint32_t xCell = sampleIdx % kSampleSide;
    const uint32_t yCell = sampleIdx / kSampleSide;
    const uint32_t upper = (xCell ^ (scrambleX >> 16)) << 16;
    const uint32_t reversedUpper =
        _ReverseBitsLowDiscrepancy(upper) ^ scrambleY;
    const uint32_t delta =
        (yCell << 16) ^ (reversedUpper & 0xFFFF0000u);
    const uint32_t lower = _InverseReverseBitsLowDiscrepancy(delta);
    const uint32_t index = upper | lower;
    const uint32_t x = index ^ scrambleX;
    const uint32_t y = reversedUpper ^ delta;
    const float jitterX = (x & 65535) * (1.0f / 65536.0f);
    const float jitterY = (y & 65535) * (1.0f / 65536.0f);
    uint32_t z = scrambleZ;
    uint32_t remainingIndex = index;
    for (uint64_t v = uint64_t(3) << 62; remainingIndex;
         remainingIndex >>= 1, v ^= v >> 1) {
        if (remainingIndex & 1) {
            z ^= uint32_t(v >> 31);
        }
    }
    return Imath::V3f(
        (xCell + jitterX) / kSampleSide,
        (yCell + jitterY) / kSampleSide,
        z * 2.3283063e-10f);
}

uint64_t
_MixHash(uint64_t hash)
{
    hash ^= hash >> 23;
    hash *= 0x2127599bf4325c37ULL;
    hash ^= hash >> 47;
    return hash;
}

uint64_t
_Hash(std::initializer_list<uint64_t> values)
{
    const uint64_t multiplier = 0x880355f21e6d1965ULL;
    uint64_t hash = values.size() * sizeof(uint64_t) * multiplier;
    for (const uint64_t value : values) {
        hash ^= _MixHash(value);
        hash *= multiplier;
    }
    return _MixHash(hash);
}

uint32_t
_Hash3(uint32_t x, uint32_t y, uint32_t z)
{
    return _Hash({(uint64_t(x) << 32) + y, uint64_t(z)});
}

float
_TransmissionAlbedo(int iorIdx, int roughnessIdx, int cosThetaIdx,
                    bool backfacing)
{
    const float fresnelIndex =
        float(iorIdx) / float(kIorCount - 1);
    const float roughness =
        float(roughnessIdx) / float(kRoughnessCount - 1);
    const float cosTheta =
        bsdl::mtx::DielectricBSDF<bsdl::mtx::DielectricFresnel>::get_cosine(
            cosThetaIdx);
    const bsdl::mtx::DielectricFresnel fresnel =
        bsdl::mtx::DielectricFresnel::from_table_index(
            fresnelIndex, backfacing);
    if (roughnessIdx == 0) {
        return 1.0f - fresnel.eval(cosTheta).max();
    }

    const bsdl::mtx::DielectricBSDF<bsdl::mtx::DielectricFresnel> bsdf(
        bsdl::GGXDist(roughness, 0.0f), fresnel, cosTheta, roughness, true);
    const Imath::V3f omegaOut(
        std::sqrt(1.0f - cosTheta * cosTheta), 0.0f, cosTheta);
    const uint32_t scrambleX = _Hash3(iorIdx, roughnessIdx, 0);
    const uint32_t scrambleY = _Hash3(iorIdx, roughnessIdx, 1);
    const uint32_t scrambleZ = _Hash3(iorIdx, roughnessIdx, 2);
    float albedo = 0.0f;
    for (int sampleIdx = 0; sampleIdx < kSampleCount; ++sampleIdx) {
        const Imath::V3f random =
            _GetSample(sampleIdx, scrambleX, scrambleY, scrambleZ);
        const bsdl::Sample sample =
            bsdf.sample(omegaOut, random.x, random.y, random.z);
        const float transmitted = sample.wi.z < 0.0f
            ? sample.weight.max()
            : 0.0f;
        albedo = bsdl::LERP(
            1.0f / (1.0f + sampleIdx), albedo, transmitted);
    }
    return std::clamp(albedo, 0.0f, 1.0f);
}

std::vector<float>
_GenerateTable(bool backfacing)
{
    std::vector<float> values(kValueCount);
    std::atomic<int> nextIor(0);
    const unsigned int threadCount = std::max(1U, std::thread::hardware_concurrency());
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (unsigned int threadIdx = 0; threadIdx < threadCount; ++threadIdx) {
        workers.emplace_back([&]() {
            for (int iorIdx = nextIor.fetch_add(1); iorIdx < kIorCount;
                 iorIdx = nextIor.fetch_add(1)) {
                for (int roughnessIdx = 0; roughnessIdx < kRoughnessCount;
                     ++roughnessIdx) {
                    for (int cosThetaIdx = 0; cosThetaIdx < kCosThetaCount;
                         ++cosThetaIdx) {
                        const int idx =
                            (iorIdx * kRoughnessCount + roughnessIdx) *
                                kCosThetaCount +
                            cosThetaIdx;
                        values[idx] = _TransmissionAlbedo(
                            iorIdx, roughnessIdx, cosThetaIdx, backfacing);
                    }
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    return values;
}

void
_WriteTable(FILE* file, const char* name, const std::vector<float>& values)
{
    std::fprintf(file, "constexpr float %s[kBsdlDielectricTransmissionValueCount] = {\n", name);
    for (int idx = 0; idx < kValueCount; idx += 8) {
        std::fprintf(file, "    ");
        for (int column = 0; column < 8 && idx + column < kValueCount;
             ++column) {
            const float value = values[idx + column];
            if (value == float(int(value))) {
                std::fprintf(file, "%d.0f,", int(value));
            } else {
                std::fprintf(file, "%.9gf,", value);
            }
            if (column != 7 && idx + column + 1 < kValueCount) {
                std::fprintf(file, " ");
            }
        }
        std::fprintf(file, "\n");
    }
    std::fprintf(file, "};\n\n");
}

} // namespace

int
main(int argc, const char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-header>\n", argv[0]);
        return 1;
    }

    const std::vector<float> front = _GenerateTable(false);
    const std::vector<float> back = _GenerateTable(true);
    FILE* file = std::fopen(argv[1], "wb");
    if (!file) {
        std::fprintf(stderr, "failed to open %s\n", argv[1]);
        return 1;
    }
    std::fprintf(file,
        "//\n// Copyright 2024 Pixar\n//\n"
        "// Licensed under the terms set forth in the LICENSE.txt file available at\n"
        "// https://openusd.org/license.\n//\n"
        "// BSDL coupled-dielectric directional transmission albedo. Generated by\n"
        "// generateHdEmbreeDielectricTransmissionLut from stock BSDL's bounded\n"
        "// reflection-VNDF sampler. Roughness zero is analytic exact Fresnel.\n//\n"
        "// Axis order is IOR, perceptual roughness, then cosThetaO. The IOR\n"
        "// coordinate is sqrt((ior - 1.001) / (5.0 - 1.001)); roughness and\n"
        "// cosThetaO are linear.\n//\n"
        "#ifndef MXCPP_MATERIALS_BSDF_DIELECTRIC_TRANSMISSION_LUT_H\n"
        "#define MXCPP_MATERIALS_BSDF_DIELECTRIC_TRANSMISSION_LUT_H\n\n"
        "namespace mxcpp {\nnamespace bsdf_luts {\n\n"
        "constexpr int kBsdlDielectricTransmissionCosThetaCount = 16;\n"
        "constexpr int kBsdlDielectricTransmissionRoughnessCount = 16;\n"
        "constexpr int kBsdlDielectricTransmissionIorCount = 32;\n"
        "constexpr int kBsdlDielectricTransmissionValueCount =\n"
        "    kBsdlDielectricTransmissionCosThetaCount *\n"
        "    kBsdlDielectricTransmissionRoughnessCount *\n"
        "    kBsdlDielectricTransmissionIorCount;\n\n");
    _WriteTable(file,
        "kBsdlDielectricTransmissionFrontSingleScatterAlbedo", front);
    _WriteTable(file,
        "kBsdlDielectricTransmissionBackSingleScatterAlbedo", back);
    std::fprintf(file,
        "} // namespace bsdf_luts\n} // namespace mxcpp\n\n"
        "#endif\n");
    std::fclose(file);
    return 0;
}
