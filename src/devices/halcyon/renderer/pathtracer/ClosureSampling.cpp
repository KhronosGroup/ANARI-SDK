// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// The composite nodes of the closure traversal's sampling (SampleNode()'s
// MixData, LayerData, AddData and MultiplyData cases, _FinalizeSubtreeSample(),
// _ApproxWeight() and _EvalThroughput(), which they need and which are
// file-local there) ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main
// at 70c45e83b, pxr/imaging/plugin/hdEmbree/renderer/materials/MaterialXCpp/
// materials/bsdf/closureTraversal.cpp, Copyright 2024 Pixar, Tomorrow Open
// Source Technology License 1.0; see external/typhoon/LICENSE.txt). Only the
// names changed, and the mix's weighting of delta samples (ClosureSampling.h).

#include "ClosureSampling.h"
// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/dielectric.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/energyCompensation.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/fresnel.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/mathPrimitives.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/reflectionOnlyInterfaces.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/shadingFrame.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/sheen.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/thinFilm.h>
// std
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace halcyon::pathtracer {

namespace {

using mxcpp::SurfaceInteraction;
using mxcpp::Vec3f;
namespace Bsdf = mxcpp::Bsdf;
// Typhoon's code is in mxcpp::Bsdf::detail, and sees mxcpp's helpers
using namespace mxcpp;
using namespace mxcpp::Bsdf::detail;

Vec3f evalThroughput(const Bsdf::ClosureTree &tree,
    Bsdf::NodeId nodeId,
    const SurfaceInteraction &interaction);

// _EvalLayerBaseThroughput()
Vec3f evalLayerBaseThroughput(const Bsdf::ClosureTree &tree,
    Bsdf::NodeId topNodeId,
    const SurfaceInteraction &interaction)
{
  return evalThroughput(tree, topNodeId, interaction);
}

// _EvalThroughput()
Vec3f evalThroughput(const Bsdf::ClosureTree &tree,
    Bsdf::NodeId nodeId,
    const SurfaceInteraction &interaction)
{
  const Bsdf::Node *node = tree.Get(nodeId);
  if (!node) {
    return Vec3f(0.0f);
  }

  return std::visit(
      [&](const auto &data) -> Vec3f {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Bsdf::OrenNayarDiffuseData>
            || std::is_same_v<T, Bsdf::BurleyDiffuseData>
            || std::is_same_v<T, Bsdf::TranslucentData>
            || std::is_same_v<T, Bsdf::SubsurfaceData>
            || std::is_same_v<T, Bsdf::UnsupportedData>) {
          return Vec3f(0.0f);
        } else if constexpr (std::is_same_v<T, Bsdf::DielectricData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const float effectiveIor =
              ResolveDielectricIor(data, interaction.heroWavelengthNm);
          Vec3f throughputRgb(1.0f);
          if (data.scatterMode != Bsdf::ScatterMode::Transmission) {
            const bool useDirectionalLayerThroughput = !HasThinFilm(
                data.thinFilmWeight, data.thinFilmThickness, data.thinFilmIor);
            if (useDirectionalLayerThroughput) {
              if (GetDielectricThroughputModeState()
                  == Bsdf::DielectricLayerThroughputMode::MaterialXGlsl) {
                const Vec3f reflectance =
                    MaterialXGlslDielectricLayerReflectance(
                        AverageAlphaForEnergy(data.roughness),
                        NdotV,
                        effectiveIor);
                throughputRgb -= reflectance * data.weight;
              } else {
                const float filter = LookupBsdlDielectricReflFrontFilter(NdotV,
                    BsdlLayerRoughnessFromAlpha(data.roughness),
                    effectiveIor);
                throughputRgb =
                    LerpVec(Vec3f(1.0f), Vec3f(filter), Clamp01(data.weight));
              }
            } else {
              const Vec3f reflectance = LayerThroughputReflectance(
                  AverageAlphaForEnergy(data.roughness),
                  NdotV,
                  DielectricReflectionFresnelUntinted(
                      data, NdotV, effectiveIor));
              throughputRgb -= reflectance * data.weight;
            }
          }
          return Clamp01(throughputRgb);
        } else if constexpr (std::is_same_v<T, Bsdf::DielectricInterfaceData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const float effectiveIor =
              ResolveDielectricIor(data, interaction.heroWavelengthNm);
          Vec3f throughputRgb(1.0f);
          if (data.reflectionWeight > 0.0f) {
            const bool useDirectionalLayerThroughput = !HasThinFilm(
                data.thinFilmWeight, data.thinFilmThickness, data.thinFilmIor);
            if (useDirectionalLayerThroughput) {
              if (GetDielectricThroughputModeState()
                  == Bsdf::DielectricLayerThroughputMode::MaterialXGlsl) {
                const Vec3f reflectance =
                    MaterialXGlslDielectricLayerReflectance(
                        AverageAlphaForEnergy(data.roughness),
                        NdotV,
                        effectiveIor);
                throughputRgb -= reflectance * Clamp01(data.reflectionWeight);
              } else {
                const float filter = LookupBsdlDielectricReflFrontFilter(NdotV,
                    BsdlLayerRoughnessFromAlpha(data.roughness),
                    effectiveIor);
                throughputRgb = LerpVec(
                    Vec3f(1.0f), Vec3f(filter), Clamp01(data.reflectionWeight));
              }
            } else {
              const Vec3f reflectance = LayerThroughputReflectance(
                  AverageAlphaForEnergy(data.roughness),
                  NdotV,
                  DielectricInterfaceReflectanceUntinted(
                      data, NdotV, effectiveIor, !interaction.frontFacing));
              throughputRgb -= reflectance * Clamp01(data.reflectionWeight);
            }
          }
          return Clamp01(throughputRgb);
        } else if constexpr (std::is_same_v<T, Bsdf::ConductorData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const Vec3f reflectance =
              LayerThroughputReflectance(AverageAlphaForEnergy(data.roughness),
                  NdotV,
                  ConductorReflectionFresnel(data, NdotV));
          return Clamp01(Vec3f(1.0f) - reflectance * data.weight);
        } else if constexpr (std::is_same_v<T, Bsdf::GeneralizedSchlickData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const Vec3f reflectance =
              LayerThroughputReflectance(AverageAlphaForEnergy(data.roughness),
                  NdotV,
                  GeneralizedSchlickReflectionFresnel(data, NdotV));
          return Clamp01(Vec3f(1.0f) - reflectance * data.weight);
        } else if constexpr (std::is_same_v<T, Bsdf::SheenData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          float dirAlbedo = ApproxSheenDirAlbedo(NdotV, data.roughness);
          return Clamp01(Vec3f(1.0f - dirAlbedo * data.weight));
        } else if constexpr (std::is_same_v<T, Bsdf::AdobeOpenPbrData>) {
          return Vec3f(1.0f);
        } else if constexpr (std::is_same_v<T, Bsdf::MixData>) {
          return LerpVec(evalThroughput(tree, data.bg, interaction),
              evalThroughput(tree, data.fg, interaction),
              Clamp01(data.mix));
        } else if constexpr (std::is_same_v<T, Bsdf::LayerData>) {
          return CompMul(evalThroughput(tree, data.top, interaction),
              evalThroughput(tree, data.base, interaction));
        } else if constexpr (std::is_same_v<T, Bsdf::AddData>) {
          Vec3f t = evalThroughput(tree, data.in1, interaction)
              + evalThroughput(tree, data.in2, interaction) - Vec3f(1.0f);
          return Vec3f(
              std::max(t[0], 0.0f), std::max(t[1], 0.0f), std::max(t[2], 0.0f));
        } else if constexpr (std::is_same_v<T, Bsdf::MultiplyData>) {
          return evalThroughput(tree, data.input, interaction);
        } else {
          return Vec3f(0.0f);
        }
      },
      node->data);
}

// _ApproxWeight()
float approxWeight(const Bsdf::ClosureTree &tree,
    Bsdf::NodeId nodeId,
    const SurfaceInteraction &interaction)
{
  const auto luminance = [&](const Vec3f &value) {
    return Bsdf::detail::Luminance(value, tree.luminanceCoefficients);
  };
  const Bsdf::Node *node = tree.Get(nodeId);
  if (!node) {
    return 0.0f;
  }

  return std::visit(
      [&](const auto &data) -> float {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Bsdf::OrenNayarDiffuseData>
            || std::is_same_v<T, Bsdf::BurleyDiffuseData>) {
          return data.weight * std::max(luminance(data.color), 0.0f);
        } else if constexpr (std::is_same_v<T, Bsdf::TranslucentData>) {
          return data.weight * std::max(luminance(data.color), 0.0f);
        } else if constexpr (std::is_same_v<T, Bsdf::SubsurfaceData>) {
          return data.weight * std::max(luminance(data.color), 0.0f);
        } else if constexpr (std::is_same_v<T, Bsdf::DielectricData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const float effectiveIor =
              ResolveDielectricIor(data, interaction.heroWavelengthNm);
          const Vec3f reflectance =
              DielectricReflectionFresnelUntinted(data, NdotV, effectiveIor);
          const float baseReflectance =
              SchlickFresnelScalar(effectiveIor, NdotV);
          const Vec3f transmissionScale =
              TransmissionScale(baseReflectance, reflectance);
          if (data.scatterMode == Bsdf::ScatterMode::Reflection) {
            return std::max(luminance(reflectance) * data.weight, 0.05f);
          }
          if (data.scatterMode == Bsdf::ScatterMode::Transmission) {
            return std::max(
                data.weight * luminance(CompMul(data.tint, transmissionScale)),
                0.05f);
          }
          return std::max(data.weight
                  * (luminance(reflectance)
                      + luminance(CompMul(data.tint, transmissionScale))),
              0.05f);
        } else if constexpr (std::is_same_v<T, Bsdf::DielectricInterfaceData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const float effectiveIor =
              ResolveDielectricIor(data, interaction.heroWavelengthNm);
          const Vec3f reflection = DielectricInterfaceReflectionCoefficient(
              data, NdotV, effectiveIor, !interaction.frontFacing);
          const Vec3f transmission = DielectricInterfaceTransmissionCoefficient(
              data, NdotV, effectiveIor, !interaction.frontFacing);
          const float weight = luminance(reflection) + luminance(transmission);
          return weight > 0.0f ? std::max(weight, 0.05f) : 0.0f;
        } else if constexpr (std::is_same_v<T, Bsdf::ConductorData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          return data.weight
              * std::max(luminance(ConductorReflectionFresnel(data,
                             std::max(std::abs(Dot(normalShdLobeWldOut,
                                          interaction.omegaOutWld)),
                                 kEpsilon))),
                  0.05f);
        } else if constexpr (std::is_same_v<T, Bsdf::GeneralizedSchlickData>) {
          const Vec3f normalShdLobeWldOut = data.normal;
          const float NdotV = std::max(
              std::abs(Dot(normalShdLobeWldOut, interaction.omegaOutWld)),
              kEpsilon);
          const Vec3f reflectance =
              GeneralizedSchlickReflectionFresnel(data, NdotV);
          if (data.scatterMode == Bsdf::ScatterMode::Transmission) {
            const float avgF0 = Clamp01(luminance(Clamp01(data.color0)));
            const float sqrtF0 = std::sqrt(std::max(avgF0, 0.01f));
            const float ior = (1.0f + sqrtF0) / (1.0f - sqrtF0);
            return std::max(data.weight
                    * luminance(TransmissionScale(
                        SchlickFresnelScalar(ior, NdotV), reflectance)),
                0.05f);
          }
          if (data.scatterMode == Bsdf::ScatterMode::ReflectionTransmission) {
            const float avgF0 = Clamp01(luminance(Clamp01(data.color0)));
            const float sqrtF0 = std::sqrt(std::max(avgF0, 0.01f));
            const float ior = (1.0f + sqrtF0) / (1.0f - sqrtF0);
            return std::max(data.weight
                    * (luminance(reflectance)
                        + luminance(TransmissionScale(
                            SchlickFresnelScalar(ior, NdotV), reflectance))),
                0.05f);
          }
          return data.weight * std::max(luminance(reflectance), 0.05f);
        } else if constexpr (std::is_same_v<T, Bsdf::SheenData>) {
          return data.weight * std::max(luminance(data.color), 0.0f) * 0.25f;
        } else if constexpr (std::is_same_v<T, Bsdf::AdobeOpenPbrData>) {
          return 1.0f;
        } else if constexpr (std::is_same_v<T, Bsdf::UnsupportedData>) {
          return 0.0f;
        } else if constexpr (std::is_same_v<T, Bsdf::MixData>) {
          return LerpVec(Vec3f(approxWeight(tree, data.bg, interaction)),
              Vec3f(approxWeight(tree, data.fg, interaction)),
              Clamp01(data.mix))[0];
        } else if constexpr (std::is_same_v<T, Bsdf::LayerData>) {
          return approxWeight(tree, data.top, interaction)
              + approxWeight(tree, data.base, interaction)
              * luminance(evalThroughput(tree, data.top, interaction));
        } else if constexpr (std::is_same_v<T, Bsdf::AddData>) {
          return approxWeight(tree, data.in1, interaction)
              + approxWeight(tree, data.in2, interaction);
        } else if constexpr (std::is_same_v<T, Bsdf::MultiplyData>) {
          return approxWeight(tree, data.input, interaction)
              * std::max(luminance(data.weight), 0.0f);
        } else {
          return 0.0f;
        }
      },
      node->data);
}

// _FinalizeSubtreeSample(): a sample that isn't delta or subsurface takes
// the subtree's value, its cosine and pdf (EvalNode() and EvalNodeCosine()
// compute what Typhoon's one _EvalNode() call returns)
Bsdf::BsdfSample finalizeSubtreeSample(const Bsdf::ClosureTree &tree,
    Bsdf::NodeId nodeId,
    const SurfaceInteraction &interaction,
    Bsdf::BsdfSample sample)
{
  if (sample.isSubsurface) {
    sample.pdfSolidAngle = std::max(sample.pdfSolidAngle, 1.0f);
    return sample;
  }
  if (sample.pdfSolidAngle <= 0.0f)
    return sample;
  if (sample.isSpecular) {
    sample.pdfSolidAngle = std::max(sample.pdfSolidAngle, 1.0f);
    return sample;
  }
  sample.bsdfValue = EvalNode(tree,
      nodeId,
      interaction,
      sample.omegaInWld,
      BumpShadowingContext::Sampling);
  sample.bsdfValueCosine = EvalNodeCosine(tree,
      nodeId,
      interaction,
      sample.omegaInWld,
      BumpShadowingContext::Sampling);
  sample.pdfSolidAngle = PdfNode(tree, nodeId, interaction, sample.omegaInWld);
  return sample;
}

// Whether the tree has a mix node, where the two samplings can differ
bool hasMix(const Bsdf::ClosureTree &tree)
{
  for (const Bsdf::Node &node : tree.nodes) {
    if (std::holds_alternative<Bsdf::MixData>(node.data))
      return true;
  }
  return false;
}

} // namespace

Bsdf::BsdfSample sampleNode(const Bsdf::ClosureTree &tree,
    Bsdf::NodeId nodeId,
    const SurfaceInteraction &interaction,
    float u1,
    float u2,
    float uChoice)
{
  const auto luminance = [&](const Vec3f &value) {
    return Luminance(value, tree.luminanceCoefficients);
  };
  const Bsdf::Node *node = tree.Get(nodeId);
  if (!node)
    return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};

  if (const auto *data = std::get_if<Bsdf::MixData>(&node->data)) {
    float mix = Clamp01(data->mix);
    bool chooseFg = (uChoice < mix);
    Bsdf::NodeId chosen = chooseFg ? data->fg : data->bg;
    float chooseProb = chooseFg ? mix : (1.0f - mix);
    float remapped = (uChoice < mix)
        ? (mix > 0.0f ? uChoice / mix : 0.0f)
        : ((1.0f - mix) > 0.0f ? (uChoice - mix) / (1.0f - mix) : 0.0f);
    auto sample = sampleNode(tree, chosen, interaction, u1, u2, remapped);
    // Typhoon divides delta samples by 'chooseProb' too: the mix evaluates
    // to the lerp of its children, so a delta sample of the chosen child
    // already has its expected weight. Subsurface samples keep Typhoon's
    // division, which is right only at the mix whose weight the subsurface
    // leaf repeats as its own (OpenPBR's, standard_surface's) and
    // overweights them at every other (kept as Typhoon's, a maintainer
    // decision; ClosureSampling.h)
    if (sample.isSubsurface && sample.pdfSolidAngle > 0.0f
        && chooseProb > 0.0f) {
      sample.bsdfValue /= chooseProb;
    }
    return finalizeSubtreeSample(tree, nodeId, interaction, sample);
  } else if (const auto *data = std::get_if<Bsdf::LayerData>(&node->data)) {
    float topWeight = approxWeight(tree, data->top, interaction);
    float baseWeight = approxWeight(tree, data->base, interaction)
        * luminance(evalThroughput(tree, data->top, interaction));
    float total = topWeight + baseWeight;
    if (total <= 0.0f)
      return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    float pTop = topWeight / total;
    bool chooseTop = (uChoice < pTop);
    Bsdf::NodeId chosen = chooseTop ? data->top : data->base;
    float chooseProb = chooseTop ? pTop : (1.0f - pTop);
    float remapped = (uChoice < pTop)
        ? (pTop > 0.0f ? uChoice / pTop : 0.0f)
        : ((1.0f - pTop) > 0.0f ? (uChoice - pTop) / (1.0f - pTop) : 0.0f);
    auto sample = sampleNode(tree, chosen, interaction, u1, u2, remapped);
    if ((sample.isSpecular || sample.isSubsurface)
        && sample.pdfSolidAngle > 0.0f) {
      if (!chooseTop) {
        sample.bsdfValue = CompMul(sample.bsdfValue,
            evalLayerBaseThroughput(tree, data->top, interaction));
      }
      if (chooseProb > 0.0f)
        sample.bsdfValue /= chooseProb;
    }
    return finalizeSubtreeSample(tree, nodeId, interaction, sample);
  } else if (const auto *data = std::get_if<Bsdf::AddData>(&node->data)) {
    float w1 = approxWeight(tree, data->in1, interaction);
    float w2 = approxWeight(tree, data->in2, interaction);
    float total = w1 + w2;
    if (total <= 0.0f)
      return Bsdf::BsdfSample{Vec3f(0.0f), Vec3f(0.0f), 0.0f, false};
    float p1 = w1 / total;
    bool chooseFirst = (uChoice < p1);
    Bsdf::NodeId chosen = chooseFirst ? data->in1 : data->in2;
    float chooseProb = chooseFirst ? p1 : (1.0f - p1);
    float remapped = (uChoice < p1)
        ? (p1 > 0.0f ? uChoice / p1 : 0.0f)
        : ((1.0f - p1) > 0.0f ? (uChoice - p1) / (1.0f - p1) : 0.0f);
    auto sample = sampleNode(tree, chosen, interaction, u1, u2, remapped);
    if ((sample.isSpecular || sample.isSubsurface)
        && sample.pdfSolidAngle > 0.0f && chooseProb > 0.0f) {
      sample.bsdfValue /= chooseProb;
    }
    return finalizeSubtreeSample(tree, nodeId, interaction, sample);
  } else if (const auto *data = std::get_if<Bsdf::MultiplyData>(&node->data)) {
    auto sample = sampleNode(tree, data->input, interaction, u1, u2, uChoice);
    if ((sample.isSpecular || sample.isSubsurface)
        && sample.pdfSolidAngle > 0.0f) {
      sample.bsdfValue = CompMul(data->weight, sample.bsdfValue);
    }
    return finalizeSubtreeSample(tree, nodeId, interaction, sample);
  }
  // A leaf: Typhoon's sampling, its validity check and finalization (the
  // composites above have no lobe normal, so Typhoon's check after them does
  // nothing)
  return SampleNode(tree, nodeId, interaction, u1, u2, uChoice);
}

Bsdf::BsdfSample sampleSurface(const mxcpp::SurfaceClosure &closure,
    const SurfaceInteraction &interaction,
    float u1,
    float u2,
    float uLobe)
{
  Bsdf::BsdfSample sample =
      Bsdf::SampleSurface(closure, interaction, u1, u2, uLobe);
  // Only a delta sample through a mix differs (Typhoon's leaves no other
  // trace of its choices); sampling it again takes the same choices to the
  // same leaf. SampleSurface() leaves delta samples' values to the tree.
  if (sample.isSpecular && sample.pdfSolidAngle > 0.0f && closure.HasBsdfTree()
      && hasMix(closure.bsdfTree)) {
    sample = sampleNode(
        closure.bsdfTree, closure.bsdfTree.root, interaction, u1, u2, uLobe);
  }
  return sample;
}

} // namespace halcyon::pathtracer
