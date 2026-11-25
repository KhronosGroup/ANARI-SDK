// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// halcyon's BSDF sampling of closures (renderer/pathtracer/ClosureSampling.*)
// against Typhoon's (the vendored Bsdf::SampleSurface()):
// - every sample that doesn't take a delta lobe through a mix node is
//   Typhoon's, bitwise (trees without a mix, the non-delta samples of trees
//   with one, and subsurface samples, which keep Typhoon's weighting, also
//   under an enclosing mix, which overweights them: pinned, kept as
//   Typhoon's by a maintainer decision);
// - the deliberate difference from Typhoon, pinned: a delta sample through
//   mixes has the same direction, pdf and flags, and Typhoon's value is
//   halcyon's divided by the probability of each mix's choice (exactly,
//   where the mixes are above every other composite node);
// - lossless mixes of a Lambertian and a delta mirror reflect 1 by BSDF
//   sampling alone (their directional albedo, estimated over stratified
//   random numbers), where Typhoon's reflect about 1 + the mirror's share x
//   (1 / mix - 1).

#include "renderer/pathtracer/ClosureSampling.h"
// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
// std
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {

using mxcpp::SurfaceClosure;
using mxcpp::SurfaceInteraction;
using mxcpp::Vec2f;
using mxcpp::Vec3f;
namespace Bsdf = mxcpp::Bsdf;
using Bsdf::BsdfSample;
using Bsdf::NodeId;

bool report(const std::string &name, bool ok)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  return ok;
}

// Leaves //////////////////////////////////////////////////////////////////////

NodeId lambert(Bsdf::ClosureTree &t)
{
  Bsdf::OrenNayarDiffuseData d;
  d.color = Vec3f(1.f);
  return t.Add(d);
}

// A delta mirror of reflectance 1
NodeId mirror(Bsdf::ClosureTree &t)
{
  Bsdf::GeneralizedSchlickData d;
  d.color0 = Vec3f(1.f);
  d.roughness = Vec2f(0.f, 0.f);
  return t.Add(d);
}

// A smooth dielectric of IOR 1.5, reflecting or reflecting and refracting
NodeId glass(Bsdf::ClosureTree &t, Bsdf::ScatterMode mode)
{
  Bsdf::DielectricData d;
  d.roughness = Vec2f(0.f, 0.f);
  d.scatterMode = mode;
  return t.Add(d);
}

NodeId mix(Bsdf::ClosureTree &t, NodeId bg, NodeId fg, float m)
{
  Bsdf::MixData d;
  d.bg = bg;
  d.fg = fg;
  d.mix = m;
  return t.Add(d);
}

NodeId layer(Bsdf::ClosureTree &t, NodeId top, NodeId base)
{
  Bsdf::LayerData d;
  d.top = top;
  d.base = base;
  return t.Add(d);
}

// The trees ///////////////////////////////////////////////////////////////////

struct Case
{
  std::string name;
  SurfaceClosure closure;
  // the probability of the mixes' choices of a delta sample for uLobe 'u',
  // where the tree's mixes are above every other composite (else 0)
  std::function<float(float u)> mixChoice;
  // the probabilities a mix may have chosen a delta sample with, where it
  // lies below other composites (which may hold delta lobes of their own)
  std::vector<float> mixProbabilities;
};

SurfaceClosure closureOf(Bsdf::ClosureTree tree, NodeId root)
{
  SurfaceClosure c;
  tree.root = root;
  c.bsdfTree = std::move(tree);
  c.presence = 1.f;
  return c;
}

// OpenPBR's shape with metalness 0.5: mix(mix(Lambertian, subsurface of
// weight 0.5, 0.5), a delta mirror, 0.5)
SurfaceClosure subsurfaceUnderMetal()
{
  Bsdf::ClosureTree t;
  Bsdf::SubsurfaceData sss;
  sss.weight = 0.5f;
  const NodeId base = mix(t, lambert(t), t.Add(sss), 0.5f);
  const NodeId root = mix(t, base, mirror(t), 0.5f);
  SurfaceClosure c = closureOf(t, root);
  c.subsurfaceWeight = 0.5f;
  return c;
}

std::vector<Case> cases()
{
  std::vector<Case> cs;
  {
    Bsdf::ClosureTree t;
    const NodeId root = layer(t,
        glass(t, Bsdf::ScatterMode::Reflection),
        layer(t, mirror(t), lambert(t)));
    cs.push_back({"no mix (delta layers over a Lambertian)",
        closureOf(t, root),
        nullptr,
        {}});
  }
  {
    Bsdf::ClosureTree t;
    const NodeId root = mix(t, lambert(t), mirror(t), 0.3f);
    cs.push_back({"a Lambertian mixed with a mirror",
        closureOf(t, root),
        [](float) { return 0.3f; },
        {}});
  }
  {
    // mix(mix(Lambertian, mirror, 0.4), delta reflection, 0.25)
    Bsdf::ClosureTree t;
    const NodeId inner = mix(t, lambert(t), mirror(t), 0.4f);
    const NodeId root =
        mix(t, inner, glass(t, Bsdf::ScatterMode::Reflection), 0.25f);
    cs.push_back({"nested mixes of delta lobes",
        closureOf(t, root),
        [](float u) { return u < 0.25f ? 0.25f : 0.75f * 0.4f; },
        {}});
  }
  {
    // glTF PBR's shape: a delta reflection layered over a mix of a
    // Lambertian and a mirror
    Bsdf::ClosureTree t;
    const NodeId base = mix(t, lambert(t), mirror(t), 0.5f);
    const NodeId root = layer(t, glass(t, Bsdf::ScatterMode::Reflection), base);
    cs.push_back({"a mix under a layer", closureOf(t, root), nullptr, {0.5f}});
  }
  {
    Bsdf::ClosureTree t;
    const NodeId m = mix(t, lambert(t), mirror(t), 0.7f);
    Bsdf::SheenData sheen;
    Bsdf::AddData add;
    add.in1 = m;
    add.in2 = t.Add(sheen);
    Bsdf::MultiplyData mul;
    mul.input = t.Add(add);
    mul.weight = Vec3f(0.8f, 0.6f, 0.4f);
    const NodeId root = t.Add(mul);
    cs.push_back({"a mix under an add and a multiply",
        closureOf(t, root),
        nullptr,
        {0.7f}});
  }
  {
    // smooth glass mixed with a translucent lobe (both sides of the surface)
    Bsdf::ClosureTree t;
    Bsdf::TranslucentData translucent;
    const NodeId root = mix(t,
        t.Add(translucent),
        glass(t, Bsdf::ScatterMode::ReflectionTransmission),
        0.5f);
    cs.push_back({"smooth glass mixed with a translucent lobe",
        closureOf(t, root),
        [](float) { return 0.5f; },
        {}});
  }
  {
    // OpenPBR's subsurface: the leaf's weight is the mix's (Typhoon's
    // division at the mix makes up for it, and stays)
    Bsdf::ClosureTree t;
    Bsdf::SubsurfaceData sss;
    sss.weight = 0.4f;
    const NodeId root = mix(t, lambert(t), t.Add(sss), 0.4f);
    SurfaceClosure c = closureOf(t, root);
    c.subsurfaceWeight = 0.4f;
    cs.push_back(
        {"a Lambertian mixed with subsurface scattering", c, nullptr, {}});
  }
  {
    // OpenPBR's subsurface under its metal mix (halcyon keeps Typhoon's
    // division at both mixes; testSubsurfaceUnderMix() pins it)
    cs.push_back({"subsurface under a metal mix",
        subsurfaceUnderMetal(),
        [](float) { return 0.5f; },
        {}});
  }
  return cs;
}

// A random interaction: the shading, smooth and geometric normals +z (or the
// back side), a direction towards the viewer over the surface
SurfaceInteraction randomInteraction(std::mt19937 &rng)
{
  std::uniform_real_distribution<float> u(0.f, 1.f);
  SurfaceInteraction si;
  const bool front = u(rng) < 0.75f;
  const Vec3f n = Vec3f(0.f, 0.f, front ? 1.f : -1.f);
  const float cosTheta = 0.05f + 0.95f * u(rng);
  const float phi = 6.2831853f * u(rng);
  const float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
  si.omegaOutWld =
      Vec3f(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta)
      * (front ? 1.f : -1.f);
  si.normalShdWldOut = front ? n : -n;
  si.normalSrfWldOut = si.normalShdWldOut;
  si.normalGeomWldOut = si.normalShdWldOut;
  si.frontFacing = front;
  return si;
}

// The closure as the material interface samples it: leaf normals prepared
SurfaceClosure prepared(const SurfaceClosure &c, const SurfaceInteraction &si)
{
  SurfaceClosure p = c;
  Bsdf::detail::PrepareShadingNormals(
      &p.bsdfTree, si.normalShdWldOut, si.normalGeomWldOut, si.omegaOutWld);
  return p;
}

bool same(const Vec3f &a, const Vec3f &b)
{
  return std::memcmp(&a, &b, sizeof(Vec3f)) == 0;
}

// Direction, pdf, flags and eta
bool sameExceptValue(const BsdfSample &a, const BsdfSample &b)
{
  return same(a.omegaInWld, b.omegaInWld)
      && std::memcmp(&a.pdfSolidAngle, &b.pdfSolidAngle, sizeof(float)) == 0
      && a.isSpecular == b.isSpecular && a.isSubsurface == b.isSubsurface
      && a.isTransmission == b.isTransmission
      && a.isDiffuseLike == b.isDiffuseLike
      && std::memcmp(&a.eta, &b.eta, sizeof(float)) == 0;
}

bool sameSample(const BsdfSample &a, const BsdfSample &b)
{
  return sameExceptValue(a, b) && same(a.bsdfValue, b.bsdfValue)
      && same(a.bsdfValueCosine, b.bsdfValueCosine);
}

bool testAgainstTyphoon()
{
  bool ok = true;
  for (const Case &c : cases()) {
    std::mt19937 rng(47);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    int typhoons = 0, deltaThroughMix = 0, differ = 0, valuesDiffer = 0;
    std::string first;
    for (int i = 0; i < 20000; i++) {
      const SurfaceInteraction si = randomInteraction(rng);
      const SurfaceClosure closure = prepared(c.closure, si);
      const float u1 = u(rng), u2 = u(rng), uLobe = u(rng);
      const BsdfSample h =
          halcyon::pathtracer::sampleSurface(closure, si, u1, u2, uLobe);
      const BsdfSample t = Bsdf::SampleSurface(closure, si, u1, u2, uLobe);
      // halcyon's whole traversal agrees with its shortcut
      const BsdfSample n = halcyon::pathtracer::sampleNode(
          closure.bsdfTree, closure.bsdfTree.root, si, u1, u2, uLobe);
      // (a delta sample that may have come through a mix)
      const bool deltaMix = t.isSpecular && t.pdfSolidAngle > 0.f
          && (c.mixChoice || !c.mixProbabilities.empty());
      bool agrees = sameSample(h, n);
      if (!deltaMix) {
        typhoons++;
        agrees = agrees && sameSample(h, t);
      } else {
        deltaThroughMix++;
        agrees = agrees && sameExceptValue(h, t);
        if (c.mixChoice) {
          // Typhoon divides by each mix's probability, inner first: exact
          const float m = c.mixChoice(uLobe);
          Vec3f expected = h.bsdfValue;
          if (c.name == "nested mixes of delta lobes" && uLobe >= 0.25f)
            expected = (expected / 0.4f) / 0.75f;
          else
            expected = expected / m;
          agrees = agrees && same(t.bsdfValue, expected);
        } else {
          // below other composites: within rounding, by one of the mix's
          // probabilities (or unscaled, a delta lobe not under the mix)
          bool any = same(h.bsdfValue, t.bsdfValue);
          for (float p : c.mixProbabilities) {
            bool all = true;
            for (int k = 0; k < 3; k++) {
              const float want = h.bsdfValue[k] / p;
              all = all
                  && std::abs(t.bsdfValue[k] - want)
                      <= 1e-6f * std::max(1.f, std::abs(want));
            }
            any = any || all;
          }
          agrees = agrees && any;
        }
        valuesDiffer += !same(h.bsdfValue, t.bsdfValue);
      }
      if (!agrees) {
        if (!differ)
          first = " (first at sample " + std::to_string(i) + ")";
        differ++;
      }
    }
    const bool hasMix = c.mixChoice || !c.mixProbabilities.empty();
    std::printf(
        "  %s: %d samples not delta or not through a mix, %d delta, %d of "
        "them through a mix\n",
        c.name.c_str(),
        typhoons,
        deltaThroughMix,
        valuesDiffer);
    ok &= report(c.name
            + ": Typhoon's samples bitwise but for a mix's delta samples, "
              "which Typhoon divides by the mix's probability"
            + first,
        differ == 0 && (!hasMix || valuesDiffer > 1000)
            && (!c.mixChoice || valuesDiffer == deltaThroughMix));
  }
  return ok;
}

constexpr int ALBEDO_STRATA = 64;

// The directional albedo of 'c' at a view direction of cosine 'cosTheta' by
// BSDF sampling: the mean sample weight over stratified random numbers
double albedo(const SurfaceClosure &c, float cosTheta, bool typhoon)
{
  SurfaceInteraction si;
  si.normalShdWldOut = Vec3f(0.f, 0.f, 1.f);
  si.normalSrfWldOut = si.normalShdWldOut;
  si.normalGeomWldOut = si.normalShdWldOut;
  si.omegaOutWld = Vec3f(std::sqrt(1.f - cosTheta * cosTheta), 0.f, cosTheta);
  si.frontFacing = true;
  const SurfaceClosure closure = prepared(c, si);
  constexpr int N = ALBEDO_STRATA;
  double sum = 0.;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      for (int k = 0; k < N; k++) {
        const float u1 = (i + 0.5f) / N, u2 = (j + 0.5f) / N,
                    uLobe = (k + 0.5f) / N;
        const BsdfSample s = typhoon
            ? Bsdf::SampleSurface(closure, si, u1, u2, uLobe)
            : halcyon::pathtracer::sampleSurface(closure, si, u1, u2, uLobe);
        if (!(s.pdfSolidAngle > 0.f))
          continue;
        const Vec3f w =
            s.isSpecular ? s.bsdfValue : s.bsdfValueCosine / s.pdfSolidAngle;
        sum += (w[0] + w[1] + w[2]) / 3.;
      }
    }
  }
  return sum / (double(N) * N * N);
}

// Pins today's weighting of subsurface under an enclosing mix, which is
// Typhoon's and kept by a maintainer decision (AGENTS.md, "Delta lobes in
// mixes"): each mix divides a subsurface sample by its choice's probability,
// so in mix(mix(Lambertian, subsurface of weight 0.5, 0.5), mirror, 0.5) the
// subsurface lobe is chosen a quarter of the time and weighs 0.5 / 0.5 / 0.5
// = 2, a share of 0.5 where its evaluation's is (1 - 0.5) x 0.5 = 0.25: the
// enclosing mix overweights it by 1 / (1 - metalness)
bool testSubsurfaceUnderMix()
{
  const SurfaceClosure c = subsurfaceUnderMetal();
  SurfaceInteraction si;
  si.normalShdWldOut = Vec3f(0.f, 0.f, 1.f);
  si.normalSrfWldOut = si.normalShdWldOut;
  si.normalGeomWldOut = si.normalShdWldOut;
  si.omegaOutWld = Vec3f(0.6f, 0.f, 0.8f);
  si.frontFacing = true;
  const SurfaceClosure closure = prepared(c, si);
  constexpr int N = 1024;
  int count = 0;
  bool asTyphoon = true, weighsTwo = true;
  double share = 0.;
  for (int k = 0; k < N; k++) {
    const float uLobe = (k + 0.5f) / N;
    const BsdfSample h =
        halcyon::pathtracer::sampleSurface(closure, si, 0.3f, 0.7f, uLobe);
    const BsdfSample t = Bsdf::SampleSurface(closure, si, 0.3f, 0.7f, uLobe);
    if (!h.isSubsurface)
      continue;
    count++;
    asTyphoon = asTyphoon && sameSample(h, t);
    weighsTwo = weighsTwo && same(h.bsdfValue, Vec3f(2.f));
    share += h.bsdfValue[0] / N;
  }
  std::printf("  subsurface under a metal mix: chosen %d of %d, share %.4f\n",
      count,
      N,
      share);
  return report(
      "subsurface under an enclosing mix keeps Typhoon's weighting: chosen "
      "0.25 of the time, weighing 2 (a share of 0.5 where its evaluation's "
      "is 0.25; kept as Typhoon's)",
      count == N / 4 && asTyphoon && weighsTwo);
}

bool testFurnaces()
{
  bool ok = true;
  for (float m : {0.3f, 0.5f, 0.8f}) {
    Bsdf::ClosureTree t;
    const NodeId root = mix(t, lambert(t), mirror(t), m);
    const SurfaceClosure c = closureOf(t, root);
    for (float cosTheta : {0.9f, 0.4f}) {
      const double h = albedo(c, cosTheta, false);
      const double ty = albedo(c, cosTheta, true);
      // Typhoon's: 1, plus 1 / m - 1 for the share of lobe choices (strata
      // of uLobe) that take the mirror
      int mirrors = 0;
      for (int k = 0; k < ALBEDO_STRATA; k++)
        mirrors += (k + 0.5f) / ALBEDO_STRATA < m;
      const double typhoonExpected =
          1. + double(mirrors) / ALBEDO_STRATA * (1. / m - 1.);
      std::printf(
          "  mirror share %.1f, view cosine %.1f: halcyon %.6f, Typhoon "
          "%.6f\n",
          m,
          cosTheta,
          h,
          ty);
      char name[160];
      std::snprintf(name,
          sizeof(name),
          "a white Lambertian mixed with a mirror (%.1f) reflects 1 by BSDF "
          "sampling at view cosine %.1f (Typhoon's %.4f)",
          m,
          cosTheta,
          typhoonExpected);
      ok &= report(name,
          std::abs(h - 1.) <= 1e-4 && std::abs(ty - typhoonExpected) <= 1e-4);
    }
  }
  return ok;
}

} // namespace

int main()
{
  bool ok = true;
  ok &= testAgainstTyphoon();
  ok &= testSubsurfaceUnderMix();
  ok &= testFurnaces();
  return ok ? 0 : 1;
}
