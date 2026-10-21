// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: LicenseRef-TOST-1.0
//
// Ported from Typhoon (NVIDIA-Omniverse/OpenUSD typhoon/main at 70c45e83b,
// pxr/imaging/plugin/hdEmbree/renderer/integrator/closureClassification.cpp,
// Copyright 2026 Pixar, Tomorrow Open Source Technology License 1.0; see
// external/typhoon/LICENSE.txt). Only the namespaces and names changed.

#include "ClosureClassification.h"
// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
// std
#include <type_traits>
#include <variant>

namespace halcyon::pathtracer {

namespace {

constexpr float REFLECTION_ONLY_EPS = 1.0e-6f;

bool isEffectivelyZero(float value)
{
  return value <= REFLECTION_ONLY_EPS;
}

bool isEffectivelyOpaque(float value)
{
  return value >= 1.0f - REFLECTION_ONLY_EPS;
}

bool isReflectionOnlyNode(
    const mxcpp::Bsdf::ClosureTree &tree, mxcpp::Bsdf::NodeId nodeId)
{
  const mxcpp::Bsdf::Node *const node = tree.Get(nodeId);
  if (!node)
    return false;

  const mxcpp::Bsdf::NodeData &data = node->data;

  // Whole OpenPBR closures are reflection-only when transport exits are off
  if (const auto *openPbr = std::get_if<mxcpp::Bsdf::AdobeOpenPbrData>(&data)) {
    return isEffectivelyOpaque(openPbr->geometryOpacity)
        && isEffectivelyZero(openPbr->transmissionWeight)
        && isEffectivelyZero(openPbr->subsurfaceWeight);
  }

  // Pure reflection lobes are always reflection-only
  if (std::get_if<mxcpp::Bsdf::OrenNayarDiffuseData>(&data)
      || std::get_if<mxcpp::Bsdf::BurleyDiffuseData>(&data)
      || std::get_if<mxcpp::Bsdf::ConductorData>(&data)
      || std::get_if<mxcpp::Bsdf::SheenData>(&data)) {
    return true;
  }

  // Dielectrics are reflection-only when disabled or authored that way
  if (const auto *dielectric =
          std::get_if<mxcpp::Bsdf::DielectricData>(&data)) {
    return isEffectivelyZero(dielectric->weight)
        || dielectric->scatterMode == mxcpp::Bsdf::ScatterMode::Reflection;
  }

  // Coupled dielectric interfaces are reflection-only without transmission
  if (const auto *dielectricInterface =
          std::get_if<mxcpp::Bsdf::DielectricInterfaceData>(&data)) {
    return isEffectivelyZero(dielectricInterface->transmissionWeight);
  }

  // Schlick lobes are reflection-only when disabled or authored that way
  if (const auto *schlick =
          std::get_if<mxcpp::Bsdf::GeneralizedSchlickData>(&data)) {
    return isEffectivelyZero(schlick->weight)
        || schlick->scatterMode == mxcpp::Bsdf::ScatterMode::Reflection;
  }

  // Translucent lobes classify as reflection-only when disabled
  if (const auto *translucent =
          std::get_if<mxcpp::Bsdf::TranslucentData>(&data))
    return isEffectivelyZero(translucent->weight);

  // Subsurface lobes classify as reflection-only when disabled
  if (const auto *subsurface = std::get_if<mxcpp::Bsdf::SubsurfaceData>(&data))
    return isEffectivelyZero(subsurface->weight);

  // Endpoint mixes classify the surviving branch; others require both
  if (const auto *mix = std::get_if<mxcpp::Bsdf::MixData>(&data)) {
    if (isEffectivelyZero(mix->mix))
      return isReflectionOnlyNode(tree, mix->bg);
    if (isEffectivelyOpaque(mix->mix))
      return isReflectionOnlyNode(tree, mix->fg);
    return isReflectionOnlyNode(tree, mix->fg)
        && isReflectionOnlyNode(tree, mix->bg);
  }

  // A layer is reflection-only when both children are reflection-only
  if (const auto *layer = std::get_if<mxcpp::Bsdf::LayerData>(&data)) {
    return isReflectionOnlyNode(tree, layer->top)
        && isReflectionOnlyNode(tree, layer->base);
  }

  // An add is reflection-only when both inputs are reflection-only
  if (const auto *add = std::get_if<mxcpp::Bsdf::AddData>(&data)) {
    return isReflectionOnlyNode(tree, add->in1)
        && isReflectionOnlyNode(tree, add->in2);
  }

  // Multiplication preserves its input classification
  if (const auto *multiply = std::get_if<mxcpp::Bsdf::MultiplyData>(&data))
    return isReflectionOnlyNode(tree, multiply->input);

  static_assert(std::variant_size_v<mxcpp::Bsdf::NodeData> == 15,
      "A closure node kind was added: classify it above.");
  return false;
}

bool hasCoupledThickDielectricNode(
    const mxcpp::Bsdf::ClosureTree &tree, mxcpp::Bsdf::NodeId nodeId)
{
  const mxcpp::Bsdf::Node *const node = tree.Get(nodeId);
  if (!node)
    return false;

  // Any active coupled thick interface makes the NEE origin conservative: a
  // straight shadow ray through that same object cannot represent the
  // refracted path evaluated by the BSDF strategy
  return std::visit(
      [&](const auto &data) -> bool {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, mxcpp::Bsdf::AdobeOpenPbrData>) {
          return !data.geometryThinWalled && data.specularWeight > 0.0f
              && data.transmissionWeight > 0.0f && data.baseMetalness < 1.0f;
        } else if constexpr (std::is_same_v<T,
                                 mxcpp::Bsdf::DielectricInterfaceData>) {
          return data.compensateCoupledDielectric && !data.thinWalled
              && data.reflectionWeight > 0.0f && data.transmissionWeight > 0.0f;
        } else if constexpr (std::is_same_v<T, mxcpp::Bsdf::MixData>) {
          if (data.mix <= 0.0f)
            return hasCoupledThickDielectricNode(tree, data.bg);
          if (data.mix >= 1.0f)
            return hasCoupledThickDielectricNode(tree, data.fg);
          return hasCoupledThickDielectricNode(tree, data.fg)
              || hasCoupledThickDielectricNode(tree, data.bg);
        } else if constexpr (std::is_same_v<T, mxcpp::Bsdf::LayerData>) {
          return hasCoupledThickDielectricNode(tree, data.top)
              || hasCoupledThickDielectricNode(tree, data.base);
        } else if constexpr (std::is_same_v<T, mxcpp::Bsdf::AddData>) {
          return hasCoupledThickDielectricNode(tree, data.in1)
              || hasCoupledThickDielectricNode(tree, data.in2);
        } else if constexpr (std::is_same_v<T, mxcpp::Bsdf::MultiplyData>) {
          const bool active = data.weight[0] > 0.0f || data.weight[1] > 0.0f
              || data.weight[2] > 0.0f;
          return active && hasCoupledThickDielectricNode(tree, data.input);
        } else {
          return false;
        }
      },
      node->data);
}

} // namespace

bool isReflectionOnlyClosure(const mxcpp::SurfaceClosure &closure)
{
  if (!isEffectivelyOpaque(closure.presence)
      || !isEffectivelyOpaque(closure.opacity)
      || closure.HasSubsurfaceScattering()) {
    return false;
  }

  if (closure.HasBsdfTree())
    return isReflectionOnlyNode(closure.bsdfTree, closure.bsdfTree.root);

  return isEffectivelyZero(closure.transmission)
      && isEffectivelyZero(closure.subsurfaceWeight);
}

bool isVolumeOnlyBoundary(const mxcpp::SurfaceClosure &closure)
{
  return closure.isVolumeBoundary && !closure.HasBsdfTree()
      && isEffectivelyZero(closure.opacity);
}

bool hasCoupledThickDielectric(const mxcpp::SurfaceClosure &closure)
{
  return closure.HasBsdfTree()
      && hasCoupledThickDielectricNode(closure.bsdfTree, closure.bsdfTree.root);
}

} // namespace halcyon::pathtracer
