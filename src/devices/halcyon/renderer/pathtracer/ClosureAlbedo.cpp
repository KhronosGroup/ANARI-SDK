// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ClosureAlbedo.h"
// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/sheen.h>
#include <renderer/materials/MaterialXCpp/surfaceClosure.h>
// std
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace halcyon::pathtracer {

namespace {

using mxcpp::Vec2f;
using mxcpp::Vec3f;
namespace Bsdf = mxcpp::Bsdf;

// Trees are acyclic (children are added before their parents), but a
// malformed one mustn't recurse without end
constexpr int MAX_DEPTH = 64;

// A subtree's albedo and how many of the lobes that contribute to it are
// delta lobes and how many aren't
struct Summary
{
  Vec3f albedo{0.f};
  int deltaLobes{0};
  int otherLobes{0};
};

// Each component clamped to [0, 1], 0 where it isn't finite
Vec3f clamp01(const Vec3f &v)
{
  Vec3f r;
  for (int c = 0; c < 3; c++)
    r[c] = std::isfinite(v[c]) ? std::clamp(v[c], 0.f, 1.f) : 0.f;
  return r;
}

// Fresnel reflectance at normal incidence of a dielectric of relative 'ior'
float dielectricF0(float ior)
{
  const float r = (ior - 1.f) / (ior + 1.f);
  return std::isfinite(r) ? std::clamp(r * r, 0.f, 1.f) : 0.f;
}

// Fresnel reflectance at normal incidence of a conductor of complex index
// n + ik, per channel
Vec3f conductorF0(const Vec3f &n, const Vec3f &k)
{
  Vec3f f;
  for (int c = 0; c < 3; c++) {
    const float num = (n[c] - 1.f) * (n[c] - 1.f) + k[c] * k[c];
    const float den = (n[c] + 1.f) * (n[c] + 1.f) + k[c] * k[c];
    f[c] = den > 0.f ? std::clamp(num / den, 0.f, 1.f) : 0.f;
  }
  return f;
}

Summary lobe(const Vec3f &albedo, bool delta)
{
  Summary s;
  s.albedo = albedo;
  (delta ? s.deltaLobes : s.otherLobes) = 1;
  return s;
}

bool isDelta(const Vec2f &alpha)
{
  return Bsdf::detail::IsEffectivelyDeltaAlpha(alpha);
}

Summary combine(const Summary &a, const Summary &b, const Vec3f &albedo)
{
  Summary s;
  s.albedo = albedo;
  s.deltaLobes = a.deltaLobes + b.deltaLobes;
  s.otherLobes = a.otherLobes + b.otherLobes;
  return s;
}

Summary summarize(const Bsdf::ClosureTree &tree, Bsdf::NodeId id, int depth)
{
  const Bsdf::Node *node = tree.Get(id);
  if (!node || depth > MAX_DEPTH)
    return {};

  return std::visit(
      [&](const auto &data) -> Summary {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, Bsdf::OrenNayarDiffuseData>
            || std::is_same_v<T, Bsdf::BurleyDiffuseData>
            || std::is_same_v<T, Bsdf::TranslucentData>
            || std::is_same_v<T, Bsdf::SubsurfaceData>) {
          if (data.weight <= 0.f)
            return {};
          return lobe(data.color * data.weight, false);
        } else if constexpr (std::is_same_v<T, Bsdf::SheenData>) {
          if (data.weight <= 0.f)
            return {};
          return lobe(data.color
                  * (data.weight
                      * Bsdf::detail::ApproxSheenDirAlbedo(
                          1.f, std::clamp(data.roughness, 0.f, 1.f))),
              false);
        } else if constexpr (std::is_same_v<T, Bsdf::DielectricData>) {
          if (data.weight <= 0.f)
            return {};
          const float f = dielectricF0(data.ior);
          const float scale = data.scatterMode == Bsdf::ScatterMode::Reflection
              ? f
              : data.scatterMode == Bsdf::ScatterMode::Transmission ? 1.f - f
                                                                    : 1.f;
          return lobe(
              data.tint * (data.weight * scale), isDelta(data.roughness));
        } else if constexpr (std::is_same_v<T, Bsdf::DielectricInterfaceData>) {
          const float f = dielectricF0(data.ior);
          const float r = std::max(data.reflectionWeight, 0.f) * f;
          const float t = std::max(data.transmissionWeight, 0.f) * (1.f - f);
          if (r <= 0.f && t <= 0.f)
            return {};
          return lobe(data.reflectionTint * r + data.transmissionTint * t,
              isDelta(data.roughness));
        } else if constexpr (std::is_same_v<T, Bsdf::ConductorData>) {
          if (data.weight <= 0.f)
            return {};
          return lobe(conductorF0(data.ior, data.extinction) * data.weight,
              isDelta(data.roughness));
        } else if constexpr (std::is_same_v<T, Bsdf::GeneralizedSchlickData>) {
          if (data.weight <= 0.f)
            return {};
          const Vec3f f0 = clamp01(data.color0);
          const Vec3f color = data.scatterMode == Bsdf::ScatterMode::Reflection
              ? f0
              : data.scatterMode == Bsdf::ScatterMode::Transmission
              ? Vec3f(1.f) - f0
              : Vec3f(1.f);
          return lobe(color * data.weight, isDelta(data.roughness));
        } else if constexpr (std::is_same_v<T, Bsdf::AdobeOpenPbrData>) {
          return lobe(data.baseColor * data.baseWeight, false);
        } else if constexpr (std::is_same_v<T, Bsdf::MixData>) {
          const float m = std::clamp(data.mix, 0.f, 1.f);
          if (m <= 0.f)
            return summarize(tree, data.bg, depth + 1);
          if (m >= 1.f)
            return summarize(tree, data.fg, depth + 1);
          const Summary bg = summarize(tree, data.bg, depth + 1);
          const Summary fg = summarize(tree, data.fg, depth + 1);
          return combine(bg, fg, bg.albedo * (1.f - m) + fg.albedo * m);
        } else if constexpr (std::is_same_v<T, Bsdf::LayerData>) {
          const Summary top = summarize(tree, data.top, depth + 1);
          const Summary base = summarize(tree, data.base, depth + 1);
          const Vec3f t = clamp01(top.albedo);
          return combine(
              top, base, t + mxcpp::CompMul(Vec3f(1.f) - t, base.albedo));
        } else if constexpr (std::is_same_v<T, Bsdf::AddData>) {
          const Summary a = summarize(tree, data.in1, depth + 1);
          const Summary b = summarize(tree, data.in2, depth + 1);
          return combine(a, b, a.albedo + b.albedo);
        } else if constexpr (std::is_same_v<T, Bsdf::MultiplyData>) {
          if (data.weight.x <= 0.f && data.weight.y <= 0.f
              && data.weight.z <= 0.f)
            return {};
          Summary s = summarize(tree, data.input, depth + 1);
          s.albedo = mxcpp::CompMul(s.albedo, data.weight);
          return s;
        } else {
          // Bsdf::UnsupportedData: no lobe
          return {};
        }
      },
      node->data);
}

} // namespace

ClosureAlbedo closureAlbedo(const mxcpp::SurfaceClosure &closure)
{
  ClosureAlbedo result;
  if (!closure.HasBsdfTree())
    return result;
  const Summary s = summarize(closure.bsdfTree, closure.bsdfTree.root, 0);
  result.albedo = clamp01(s.albedo);
  result.specularOnly = s.deltaLobes > 0 && s.otherLobes == 0;
  return result;
}

} // namespace halcyon::pathtracer
