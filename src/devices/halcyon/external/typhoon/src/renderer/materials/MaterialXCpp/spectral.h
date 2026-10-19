//
// Minimal spectral utilities for hero-wavelength rendering.
// Adapted from OSL libbsdl spectral tables (BSD-3-Clause).
//
#ifndef MXCPP_SPECTRAL_H
#define MXCPP_SPECTRAL_H

#include "mathTypes.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

#include <BSDL/jakobhanika_impl.h>
#include <BSDL/spectrum_decl.h>
#include <BSDL/spectrum_luts.h>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace mxcpp {
namespace Spectral {

enum class RgbColorSpace
{
    LinearRec709,
    LinearAP1
};

constexpr float kLambdaMinNm = 380.0f;
constexpr float kLambdaMaxNm = 780.0f;
constexpr float kLambdaRangeNm = kLambdaMaxNm - kLambdaMinNm;
constexpr float kLambdaStepNm = 5.0f;
constexpr std::size_t kLambdaResolution = 81;

inline float
_LookupBinWidthNm(std::size_t index)
{
    if (index == 0 || index + 1 == kLambdaResolution) {
        return 0.5f * kLambdaStepNm;
    }
    return kLambdaStepNm;
}

inline float
SampleHeroWavelength(float u)
{
    return kLambdaMinNm + std::clamp(u, 0.0f, 1.0f) * kLambdaRangeNm;
}

inline float
HeroWavelengthPdf()
{
    return 1.0f / kLambdaRangeNm;
}

inline float
CauchyDispersionIOR(float abbeNumber, float baseIor, float wavelengthNm)
{
    if (abbeNumber <= 0.0f) {
        return baseIor;
    }

    const float nD2 = std::pow(589.3f * 1.0e-3f, 2.0f);
    const float nF2 = std::pow(486.1f * 1.0e-3f, 2.0f);
    const float nC2 = std::pow(656.3f * 1.0e-3f, 2.0f);

    const float cauchyC = (baseIor - 1.0f) *
        ((nC2 * nF2) / (nC2 - nF2)) / abbeNumber;
    const float cauchyB = baseIor - cauchyC / nD2;
    const float wavelengthUm = wavelengthNm * 1.0e-3f;
    return cauchyB + cauchyC / (wavelengthUm * wavelengthUm);
}

inline int
_LookupIndex(float wavelengthNm)
{
    const float clamped = std::clamp(wavelengthNm, kLambdaMinNm, kLambdaMaxNm);
    const float shifted = (clamped - kLambdaMinNm) / kLambdaStepNm;
    return std::clamp(
        static_cast<int>(std::round(shifted)),
        0,
        static_cast<int>(kLambdaResolution - 1));
}

inline const std::array<Vec3f, kLambdaResolution>&
_GetXyzResponse()
{
    static const std::array<Vec3f, kLambdaResolution> data = {{
        Vec3f(0.001368f, 0.000039f, 0.006450f),
        Vec3f(0.002236f, 0.000064f, 0.010550f),
        Vec3f(0.004243f, 0.000120f, 0.020050f),
        Vec3f(0.007650f, 0.000217f, 0.036210f),
        Vec3f(0.014310f, 0.000396f, 0.067850f),
        Vec3f(0.023190f, 0.000640f, 0.110200f),
        Vec3f(0.043510f, 0.001210f, 0.207400f),
        Vec3f(0.077630f, 0.002180f, 0.371300f),
        Vec3f(0.134380f, 0.004000f, 0.645600f),
        Vec3f(0.214770f, 0.007300f, 1.039050f),
        Vec3f(0.283900f, 0.011600f, 1.385600f),
        Vec3f(0.328500f, 0.016840f, 1.622960f),
        Vec3f(0.348280f, 0.023000f, 1.747060f),
        Vec3f(0.348060f, 0.029800f, 1.782600f),
        Vec3f(0.336200f, 0.038000f, 1.772110f),
        Vec3f(0.318700f, 0.048000f, 1.744100f),
        Vec3f(0.290800f, 0.060000f, 1.669200f),
        Vec3f(0.251100f, 0.073900f, 1.528100f),
        Vec3f(0.195360f, 0.090980f, 1.287640f),
        Vec3f(0.142100f, 0.112600f, 1.041900f),
        Vec3f(0.095640f, 0.139020f, 0.812950f),
        Vec3f(0.057950f, 0.169300f, 0.616200f),
        Vec3f(0.032010f, 0.208020f, 0.465180f),
        Vec3f(0.014700f, 0.258600f, 0.353300f),
        Vec3f(0.004900f, 0.323000f, 0.272000f),
        Vec3f(0.002400f, 0.407300f, 0.212300f),
        Vec3f(0.009300f, 0.503000f, 0.158200f),
        Vec3f(0.029100f, 0.608200f, 0.111700f),
        Vec3f(0.063270f, 0.710000f, 0.078250f),
        Vec3f(0.109600f, 0.793200f, 0.057250f),
        Vec3f(0.165500f, 0.862000f, 0.042160f),
        Vec3f(0.225750f, 0.914850f, 0.029840f),
        Vec3f(0.290400f, 0.954000f, 0.020300f),
        Vec3f(0.359700f, 0.980300f, 0.013400f),
        Vec3f(0.433450f, 0.994950f, 0.008750f),
        Vec3f(0.512050f, 1.000000f, 0.005750f),
        Vec3f(0.594500f, 0.995000f, 0.003900f),
        Vec3f(0.678400f, 0.978600f, 0.002750f),
        Vec3f(0.762100f, 0.952000f, 0.002100f),
        Vec3f(0.842500f, 0.915400f, 0.001800f),
        Vec3f(0.916300f, 0.870000f, 0.001650f),
        Vec3f(0.978600f, 0.816300f, 0.001400f),
        Vec3f(1.026300f, 0.757000f, 0.001100f),
        Vec3f(1.056700f, 0.694900f, 0.001000f),
        Vec3f(1.062200f, 0.631000f, 0.000800f),
        Vec3f(1.045600f, 0.566800f, 0.000600f),
        Vec3f(1.002600f, 0.503000f, 0.000340f),
        Vec3f(0.938400f, 0.441200f, 0.000240f),
        Vec3f(0.854450f, 0.381000f, 0.000190f),
        Vec3f(0.751400f, 0.321000f, 0.000100f),
        Vec3f(0.642400f, 0.265000f, 0.000050f),
        Vec3f(0.541900f, 0.217000f, 0.000030f),
        Vec3f(0.447900f, 0.175000f, 0.000020f),
        Vec3f(0.360800f, 0.138200f, 0.000010f),
        Vec3f(0.283500f, 0.107000f, 0.000000f),
        Vec3f(0.218700f, 0.081600f, 0.000000f),
        Vec3f(0.164900f, 0.061000f, 0.000000f),
        Vec3f(0.121200f, 0.044580f, 0.000000f),
        Vec3f(0.087400f, 0.032000f, 0.000000f),
        Vec3f(0.063600f, 0.023200f, 0.000000f),
        Vec3f(0.046770f, 0.017000f, 0.000000f),
        Vec3f(0.032900f, 0.011920f, 0.000000f),
        Vec3f(0.022700f, 0.008210f, 0.000000f),
        Vec3f(0.015840f, 0.005723f, 0.000000f),
        Vec3f(0.011359f, 0.004102f, 0.000000f),
        Vec3f(0.008111f, 0.002929f, 0.000000f),
        Vec3f(0.005790f, 0.002091f, 0.000000f),
        Vec3f(0.004109f, 0.001484f, 0.000000f),
        Vec3f(0.002899f, 0.001047f, 0.000000f),
        Vec3f(0.002049f, 0.000740f, 0.000000f),
        Vec3f(0.001440f, 0.000520f, 0.000000f),
        Vec3f(0.001000f, 0.000361f, 0.000000f),
        Vec3f(0.000690f, 0.000249f, 0.000000f),
        Vec3f(0.000476f, 0.000172f, 0.000000f),
        Vec3f(0.000332f, 0.000120f, 0.000000f),
        Vec3f(0.000235f, 0.000085f, 0.000000f),
        Vec3f(0.000166f, 0.000060f, 0.000000f),
        Vec3f(0.000117f, 0.000042f, 0.000000f),
        Vec3f(0.000083f, 0.000030f, 0.000000f),
        Vec3f(0.000059f, 0.000021f, 0.000000f),
        Vec3f(0.000042f, 0.000015f, 0.000000f),
    }};
    return data;
}

inline const std::array<float, kLambdaResolution>&
_GetD65Illuminant()
{
    static const std::array<float, kLambdaResolution> data = {{
        49.9755f, 52.3118f, 54.6482f, 68.7015f, 82.7549f, 87.1204f, 91.4860f,
        92.4589f, 93.4318f, 90.0570f, 86.6823f, 95.7736f, 104.8650f, 110.9360f,
        117.0080f, 117.4100f, 117.8120f, 116.3360f, 114.8610f, 115.3920f,
        115.9230f, 112.3670f, 108.8110f, 109.0820f, 109.3540f, 108.5780f,
        107.8020f, 106.2960f, 104.7900f, 106.2390f, 107.6890f, 106.0470f,
        104.4050f, 104.2250f, 104.0460f, 102.0230f, 100.0000f, 98.1671f,
        96.3342f, 96.0611f, 95.7880f, 92.2368f, 88.6856f, 89.3459f, 90.0062f,
        89.8026f, 89.5991f, 88.6489f, 87.6987f, 85.4936f, 83.2886f, 83.4939f,
        83.6992f, 81.8630f, 80.0268f, 80.1207f, 80.2146f, 81.2462f, 82.2778f,
        80.2810f, 78.2842f, 74.0027f, 69.7213f, 70.6652f, 71.6091f, 72.9790f,
        74.3490f, 67.9765f, 61.6040f, 65.7448f, 69.8856f, 72.4863f, 75.0870f,
        69.3398f, 63.5927f, 55.0054f, 46.4182f, 56.6118f, 66.8054f, 65.0941f,
        63.3828f,
    }};
    return data;
}

inline const std::array<Vec3f, kLambdaResolution>&
_GetSrgbBasis()
{
    static const std::array<Vec3f, kLambdaResolution> data = {{
        Vec3f(0.327457413827055f, 0.331861713085874f, 0.340680791548052f),
        Vec3f(0.323750578270541f, 0.329688187759399f, 0.346561186624852f),
        Vec3f(0.313439461251577f, 0.327860021624697f, 0.358700493140351f),
        Vec3f(0.288879382755265f, 0.319173580231756f, 0.391947026588195f),
        Vec3f(0.239205681158886f, 0.294322583694842f, 0.466471730587333f),
        Vec3f(0.189702036890535f, 0.258697064768736f, 0.551600895598602f),
        Vec3f(0.121746067959218f, 0.188894319254765f, 0.689359610948928f),
        Vec3f(0.074578270669466f, 0.125388381991689f, 0.800033346878607f),
        Vec3f(0.044433158634034f, 0.078687060310622f, 0.876879780935314f),
        Vec3f(0.028928632128503f, 0.053143270865945f, 0.917928097443955f),
        Vec3f(0.022316653484751f, 0.042288146031342f, 0.935395200669632f),
        Vec3f(0.016911307292632f, 0.033318345502917f, 0.949770347115183f),
        Vec3f(0.014181107117967f, 0.029755948185972f, 0.956062944805240f),
        Vec3f(0.013053142677487f, 0.030331250536905f, 0.956615606890316f),
        Vec3f(0.011986163627845f, 0.030988571897301f, 0.957025264931328f),
        Vec3f(0.011288714712405f, 0.031686355188838f, 0.957024930534713f),
        Vec3f(0.010906066465652f, 0.034669961502997f, 0.954423972737066f),
        Vec3f(0.010400713481004f, 0.034551957443675f, 0.955047329020204f),
        Vec3f(0.010637360254146f, 0.040684806194830f, 0.948677833093334f),
        Vec3f(0.010907662533774f, 0.054460037369406f, 0.934632299842328f),
        Vec3f(0.011032712448099f, 0.080905287420474f, 0.908061999852269f),
        Vec3f(0.011310656591227f, 0.146348302857044f, 0.842341039463727f),
        Vec3f(0.011154642056940f, 0.379679643296617f, 0.609165715365647f),
        Vec3f(0.010148770406212f, 0.766744268654033f, 0.223106960959533f),
        Vec3f(0.008918582118838f, 0.876214747613370f, 0.114866670291336f),
        Vec3f(0.007685576338471f, 0.918491655613843f, 0.073822767895744f),
        Vec3f(0.006705708284695f, 0.940655562534437f, 0.052638728791055f),
        Vec3f(0.005995805987644f, 0.953731884533020f, 0.040272309016889f),
        Vec3f(0.005537256642342f, 0.961643279840238f, 0.032819462650959f),
        Vec3f(0.005193784241207f, 0.967200019685078f, 0.027606195927046f),
        Vec3f(0.005025362265223f, 0.970989746390046f, 0.023984891127039f),
        Vec3f(0.005136362769675f, 0.972852303563554f, 0.022011333352792f),
        Vec3f(0.005433200260540f, 0.973116594076444f, 0.021450205255997f),
        Vec3f(0.005819985902435f, 0.973351069154143f, 0.020828944509569f),
        Vec3f(0.006400572774624f, 0.973351115544369f, 0.020248311388809f),
        Vec3f(0.007449528683409f, 0.972261079731725f, 0.020289391451207f),
        Vec3f(0.008583635819377f, 0.973351021746917f, 0.018065342335913f),
        Vec3f(0.010395762465167f, 0.973148495185693f, 0.016455742234468f),
        Vec3f(0.013565433538649f, 0.971061306300914f, 0.015373260134095f),
        Vec3f(0.019384515839974f, 0.966371305955183f, 0.014244178484552f),
        Vec3f(0.032084071202002f, 0.954941967502548f, 0.012973961554335f),
        Vec3f(0.074356037845941f, 0.913578989551261f, 0.012064974134522f),
        Vec3f(0.624393724178075f, 0.364348803907687f, 0.011257478160390f),
        Vec3f(0.918310032768720f, 0.071507242540885f, 0.010182724671694f),
        Vec3f(0.949253030175051f, 0.041230434471375f, 0.009516535387237f),
        Vec3f(0.958187833329246f, 0.032423874183669f, 0.009388292728668f),
        Vec3f(0.958187751332698f, 0.031924629798200f, 0.009887619090670f),
        Vec3f(0.958187625087782f, 0.031276033173097f, 0.010536342006459f),
        Vec3f(0.955679060771746f, 0.032630370429057f, 0.011690568837445f),
        Vec3f(0.958006154893429f, 0.029530872149074f, 0.012462972887104f),
        Vec3f(0.954101573456564f, 0.031561761170246f, 0.014336665177420f),
        Vec3f(0.947607606237237f, 0.035674218270820f, 0.016718175327544f),
        Vec3f(0.938681328447549f, 0.041403005395567f, 0.019915666075002f),
        Vec3f(0.924466682751434f, 0.050604260448956f, 0.024929056163281f),
        Vec3f(0.904606025333056f, 0.063434300381700f, 0.031959673586040f),
        Vec3f(0.880412198927933f, 0.078918245293923f, 0.040669554095248f),
        Vec3f(0.847787873151700f, 0.099542742665375f, 0.052669382421940f),
        Vec3f(0.805779126623019f, 0.125595760093287f, 0.068625110514195f),
        Vec3f(0.752531853871421f, 0.157590910441680f, 0.089877232300014f),
        Vec3f(0.686439396844578f, 0.195398239044210f, 0.118162358926434f),
        Vec3f(0.618694570860610f, 0.231474474772178f, 0.149830947442133f),
        Vec3f(0.540264443959111f, 0.268852136095262f, 0.190883409341834f),
        Vec3f(0.472964416293838f, 0.296029164217928f, 0.231006403025217f),
        Vec3f(0.432701596704049f, 0.309754994441945f, 0.257543385422202f),
        Vec3f(0.405358045528392f, 0.317815883383822f, 0.276826038721536f),
        Vec3f(0.385491834974902f, 0.322990347389898f, 0.291517772810795f),
        Vec3f(0.370983584551061f, 0.326353847938009f, 0.302662506083233f),
        Vec3f(0.357608701523081f, 0.329143902278980f, 0.313247301302886f),
        Vec3f(0.348712800108393f, 0.330808726803682f, 0.320478325124633f),
        Vec3f(0.344880119344691f, 0.331482689922243f, 0.323636994707961f),
        Vec3f(0.341917877323291f, 0.331984550352389f, 0.326097308846900f),
        Vec3f(0.339531092987129f, 0.332341172522545f, 0.328127369340184f),
        Vec3f(0.337169503774367f, 0.332912009415539f, 0.329917975958888f),
        Vec3f(0.336172018527717f, 0.332919279695214f, 0.330907901216649f),
        Vec3f(0.335167443433363f, 0.333027672578856f, 0.331803633095995f),
        Vec3f(0.334421625306463f, 0.333179704673260f, 0.332396627255361f),
        Vec3f(0.334008760376402f, 0.333247030974549f, 0.332740780726824f),
        Vec3f(0.333915792790082f, 0.333259349210601f, 0.332820857081489f),
        Vec3f(0.333818454946367f, 0.333275050279383f, 0.332901731283444f),
        Vec3f(0.333672774928456f, 0.333294328448732f, 0.333025967488632f),
        Vec3f(0.333569513405591f, 0.333309424957775f, 0.333111083081497f),
    }};
    return data;
}

inline float
_IntegrateD65Y()
{
    static const float value = []() {
        const auto& d65 = _GetD65Illuminant();
        const auto& xyz = _GetXyzResponse();
        float sum = 0.0f;
        for (std::size_t i = 0; i + 1 < kLambdaResolution; ++i) {
            const float a = d65[i] * xyz[i][1];
            const float b = d65[i + 1] * xyz[i + 1][1];
            sum += 0.5f * (a + b);
        }
        return sum * kLambdaRangeNm / static_cast<float>(kLambdaResolution - 1);
    }();
    return value;
}

inline float
RgbToSpectralValue(
    const Vec3f& rgb,
    float wavelengthNm,
    RgbColorSpace colorSpace = RgbColorSpace::LinearRec709)
{
    if (colorSpace == RgbColorSpace::LinearAP1) {
        const Vec3f nonNegative(
            std::max(rgb[0], 0.0f),
            std::max(rgb[1], 0.0f),
            std::max(rgb[2], 0.0f));
        const float maximum =
            std::max(nonNegative[0], std::max(nonNegative[1], nonNegative[2]));
        const float minimum =
            std::min(nonNegative[0], std::min(nonNegative[1], nonNegative[2]));
        if (maximum <= 0.0f) {
            return 0.0f;
        }
        if (maximum - minimum < bsdl::EPSILON) {
            return maximum;
        }

        constexpr float safeMaximum = 0.7f;
        const float scale = std::max(maximum, safeMaximum);
        const Vec3f scaled = nonNegative * (safeMaximum / scale);
        const auto curve = bsdl::JakobHanikaUpsampler(
            BSDL_CONFIG::get_jakobhanika_lut(
                BSDL_CONFIG::ColorSpaceTag::ACEScg))
            .lookup(scaled[0], scaled[1], scaled[2]);
        return curve(wavelengthNm) * scale / safeMaximum;
    }

    const Vec3f basis = _GetSrgbBasis()[_LookupIndex(wavelengthNm)];
    return std::max(
        basis[0] * rgb[0] + basis[1] * rgb[1] + basis[2] * rgb[2],
        0.0f);
}

inline Vec3f
_SpectralValueToRgbUnbalanced(
    float spectralValue,
    float wavelengthNm,
    float wavelengthPdf,
    RgbColorSpace colorSpace)
{
    if (spectralValue <= 0.0f || wavelengthPdf <= 0.0f) {
        return Vec3f(0.0f);
    }

    const int index = _LookupIndex(wavelengthNm);
    const auto& bsdlData = bsdl::Spectrum::get_luts();
    const bool useAp1 = colorSpace == RgbColorSpace::LinearAP1;
    const float illuminant = useAp1
        ? bsdlData.D60_illuminant[index]
        : _GetD65Illuminant()[index];
    const float illuminantY = useAp1
        ? bsdl::Spectrum::integrate_illuminant(
            bsdlData.D60_illuminant, bsdlData.xyz_response)
        : _IntegrateD65Y();
    const float invNorm = 1.0f / std::max(illuminantY, 1.0e-8f);
    const Vec3f xyz = _GetXyzResponse()[index] *
        (illuminant * spectralValue * invNorm / wavelengthPdf);

    const float r = useAp1
        ? std::max(
            1.641023f * xyz[0] -
            0.324803f * xyz[1] -
            0.236425f * xyz[2],
            0.0f)
        : std::max(
            3.2405f * xyz[0] -
            1.5371f * xyz[1] -
            0.4985f * xyz[2],
            0.0f);
    const float g = useAp1
        ? std::max(
            -0.663663f * xyz[0] +
            1.615332f * xyz[1] +
            0.016756f * xyz[2],
            0.0f)
        : std::max(
            -0.9693f * xyz[0] +
            1.8760f * xyz[1] +
            0.0416f * xyz[2],
            0.0f);
    const float b = useAp1
        ? std::max(
            0.011722f * xyz[0] -
            0.008284f * xyz[1] +
            0.988395f * xyz[2],
            0.0f)
        : std::max(
            0.0556f * xyz[0] -
            0.2040f * xyz[1] +
            1.0572f * xyz[2],
            0.0f);
    return Vec3f(r, g, b);
}

inline Vec3f
_ComputeNeutralRoundTripScale(RgbColorSpace colorSpace)
{
    const float pdf = HeroWavelengthPdf();
    Vec3f whiteResponse(0.0f);

    for (std::size_t i = 0; i < kLambdaResolution; ++i) {
        const float wavelengthNm =
            kLambdaMinNm + static_cast<float>(i) * kLambdaStepNm;
        const float spectralWhite =
            RgbToSpectralValue(
                Vec3f(1.0f), wavelengthNm, colorSpace);
        const Vec3f reconstructed =
            _SpectralValueToRgbUnbalanced(
                spectralWhite,
                wavelengthNm,
                pdf,
                colorSpace);
        const float probability = _LookupBinWidthNm(i) / kLambdaRangeNm;
        whiteResponse += reconstructed * probability;
    }

    return Vec3f(
        1.0f / std::max(whiteResponse[0], 1.0e-8f),
        1.0f / std::max(whiteResponse[1], 1.0e-8f),
        1.0f / std::max(whiteResponse[2], 1.0e-8f));
}

inline Vec3f
_GetNeutralRoundTripScale(RgbColorSpace colorSpace)
{
    static const Vec3f rec709Scale =
        _ComputeNeutralRoundTripScale(RgbColorSpace::LinearRec709);
    static const Vec3f ap1Scale =
        _ComputeNeutralRoundTripScale(RgbColorSpace::LinearAP1);
    return colorSpace == RgbColorSpace::LinearAP1
        ? ap1Scale
        : rec709Scale;
}

inline Vec3f
SpectralValueToRgb(
    float spectralValue,
    float wavelengthNm,
    float wavelengthPdf,
    RgbColorSpace colorSpace = RgbColorSpace::LinearRec709)
{
    const Vec3f rgb =
        _SpectralValueToRgbUnbalanced(
            spectralValue,
            wavelengthNm,
            wavelengthPdf,
            colorSpace);
    return CompMul(rgb, _GetNeutralRoundTripScale(colorSpace));
}

}  // namespace Spectral
}  // namespace mxcpp

#endif  // MXCPP_SPECTRAL_H
