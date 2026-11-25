// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// Native physicallyBased's lobes (renderer/pathtracer/PathPbm.isph) against
// the vendored MaterialXCpp code they port, function by function, on random
// inputs (test_path_pbm.ispc runs the ISPC side): the GGX missing-energy
// lookup in the vendored table and Turquin's scale, the generalized Schlick
// Fresnel and the layer's reflectance, visible-normal sampling and its pdf,
// the glossy normal's correction towards Ng, BSDL's dielectric layer filter
// in its vendored table, and whole leaves through closureTraversal.cpp
// (evaluation, pdf and sampling): generalized Schlick GGX, dielectric GGX
// reflection (the clearcoat) and transmission (rough, delta, totally
// internally reflected), and sheen. test_path_pbm.ispc is built as
// PathPbm.ispc is, so the two agree bitwise where the C++ rounds as
// PathPbm.ispc does, else within rounding (native_rounding.h).
// test_path_material compares the whole material with its graph.

#include "native_rounding.h"
#include "test_path_pbm_ispc.h"

// typhoon
#include <renderer/materials/MaterialXCpp/materials/bsdf.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/closureTraversal.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/energyCompensation.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/fresnel.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/microfacet.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/shadingFrame.h>
#include <renderer/materials/MaterialXCpp/materials/bsdf/sheen.h>
// std
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace mxcpp;
namespace detail = mxcpp::Bsdf::detail;

namespace {

int g_failures = 0;

void check(bool ok, const std::string &name)
{
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
  if (!ok)
    g_failures++;
}

using native_rounding::BITWISE;
using native_rounding::ggxConditioning;
using native_rounding::OUTLIER;
using native_rounding::ROUNDING;

// Within rounding (native_rounding.h), values agree within ROUNDING times
// the computation's conditioning (ggxConditioning(), VALID_NORMAL). A few
// may still differ by more, or take the other branch of a threshold (delta
// or rough, valid, refracted): at most FEW_VALUES of them, as few as the
// refraction's change of variables 1 / (wo.h + eta wi.h)^2 leaves beyond
// tolerance where refracted directions graze (7 of 131072 values seen with
// -march=native), and those within OUTLIER times their tolerance.
constexpr double FEW_VALUES = 0.001;

// EnsureValidSpecularReflection() solves a quadratic whose discriminant
// B^2 - 4AC and root B - sqrt(disc) cancel at grazing views, so contracting
// them moves the corrected normal by up to 3.2e-4 (-march=native)
constexpr float VALID_NORMAL = 10.f;

bool fewEnough(int beyond, int count)
{
  return BITWISE ? beyond == 0 : beyond <= FEW_VALUES * count;
}

// Values compared bitwise (NaNs alike), or within rounding (above), with
// the worst relative difference
struct Comparison
{
  int count{0};
  int beyond{0};
  float worst{0.f};
  // the largest difference as a multiple of its tolerance
  float worstRatio{0.f};
  std::string where;

  void add(float a,
      float b,
      const std::string &what,
      float conditioning = 1.f)
  {
    count++;
    const bool bothNan = std::isnan(a) && std::isnan(b);
    const float err =
        bothNan ? 0.f : std::abs(a - b) / std::max(1.f, std::abs(b));
    const bool differs = BITWISE
        ? !bothNan && std::memcmp(&a, &b, sizeof(float)) != 0
        : !bothNan && !(err <= ROUNDING * conditioning);
    if (differs) {
      if (beyond++ == 0)
        where = what + ": " + std::to_string(a) + " vs " + std::to_string(b);
    }
    if (err > worst || std::isnan(err))
      worst = std::isnan(err) ? INFINITY : err;
    const float ratio = bothNan ? 0.f : err / (ROUNDING * conditioning);
    if (ratio > worstRatio || std::isnan(ratio))
      worstRatio = std::isnan(ratio) ? INFINITY : ratio;
  }

  void add3(const float *a,
      const Vec3f &b,
      const std::string &what,
      float conditioning = 1.f)
  {
    for (int k = 0; k < 3; k++)
      add(a[k], b[k], what + "[" + std::to_string(k) + "]", conditioning);
  }

  bool matches() const
  {
    return fewEnough(beyond, count) && (BITWISE || worstRatio <= OUTLIER);
  }

  std::string describe() const
  {
    char s[96];
    std::snprintf(s,
        sizeof(s),
        "%d values, worst relative difference %.2g (%.2g x tolerance)",
        count,
        double(worst),
        double(worstRatio));
    return beyond ? std::string(s) + "; " + std::to_string(beyond)
            + " beyond tolerance, first " + where
                  : std::string(s);
  }
};

std::mt19937 g_rng(1234);

float u01()
{
  return std::uniform_real_distribution<float>(0.f, 1.f)(g_rng);
}

Vec3f randomDirection()
{
  std::normal_distribution<float> d;
  Vec3f v;
  do {
    v = Vec3f(d(g_rng), d(g_rng), d(g_rng));
  } while (v.length2() < 1e-6f);
  return v.normalized();
}

// Alphas over the whole range, with the thresholds' neighbourhoods
float randomAlpha()
{
  const float special[] = {
      1e-6f, 1e-5f, 9.9e-4f, 1e-3f, 1.2e-3f, 0.0016f, 0.0017f, 0.01f, 1.f};
  const float r = u01();
  if (r < 0.3f)
    return special[int(u01() * std::size(special)) % std::size(special)];
  // perceptual roughness uniformly
  const float p = u01();
  return std::max(p * p, 1e-5f);
}

void put3(std::vector<float> &v, const Vec3f &x)
{
  v.push_back(x[0]);
  v.push_back(x[1]);
  v.push_back(x[2]);
}

// Checks /////////////////////////////////////////////////////////////////////

void checkMissingEnergy()
{
  std::vector<float> in;
  // every table node, the midpoints and random points
  for (int a = 0; a <= 30; a++) {
    for (int c = 0; c <= 30; c++) {
      const float alpha = float(a) / 30.f;
      in.push_back(float(c) / 30.f);
      in.push_back(alpha * alpha);
    }
  }
  for (int i = 0; i < 4096; i++) {
    in.push_back(u01() * 1.1f - 0.05f);
    in.push_back(randomAlpha());
  }
  const int n = int(in.size() / 2);
  std::vector<float> out(in.size());
  ispc::kernel_missingEnergy(in.data(), out.data(), n);

  Comparison energy, turquin;
  for (int i = 0; i < n; i++) {
    const float c = in[2 * i], a = in[2 * i + 1];
    energy.add(out[2 * i],
        detail::LookupGgxMissingEnergy(c, a),
        "point " + std::to_string(i));
    // TurquinMicrofacetMsScale() of F = 1 is 1 + missing / single
    const Vec3f scale = detail::TurquinMicrofacetMsScale(a, c, Vec3f(1.f));
    turquin.add(1.f + out[2 * i + 1], scale[0], "point " + std::to_string(i));
  }
  check(energy.matches(),
      "GGX missing energy from the vendored table: " + energy.describe());
  check(turquin.matches(),
      "Turquin's multiple-scattering scale: " + turquin.describe());
}

void checkFresnel()
{
  std::vector<float> in;
  for (int i = 0; i < 8192; i++) {
    // colours past 1 (a metal's base colour may be) and below 0
    for (int k = 0; k < 6; k++)
      in.push_back(u01() * 1.4f - 0.1f);
    in.push_back(i % 16 == 0 ? float(i % 3) * 0.5f : u01());
    in.push_back(randomAlpha());
  }
  const int n = int(in.size() / 8);
  std::vector<float> out(size_t(n) * 6);
  ispc::kernel_fresnel(in.data(), out.data(), n);

  Comparison fresnel, layer;
  for (int i = 0; i < n; i++) {
    const float *p = &in[8 * i];
    const Vec3f F0(p[0], p[1], p[2]), F90(p[3], p[4], p[5]);
    const Vec3f F =
        detail::GeneralizedSchlickFresnel(F0, Vec3f(1.f), F90, 5.f, p[6]);
    fresnel.add3(&out[6 * i], F, "point " + std::to_string(i));
    layer.add3(&out[6 * i + 3],
        detail::LayerThroughputReflectance(p[7], p[6], F),
        "point " + std::to_string(i));
  }
  check(fresnel.matches(),
      "generalized Schlick Fresnel (colour82 1, exponent 5): "
          + fresnel.describe());
  check(layer.matches(),
      "a GGX layer's reflectance (LayerThroughputReflectance()): "
          + layer.describe());
}

void checkVndf()
{
  std::vector<float> in;
  for (int i = 0; i < 8192; i++) {
    Vec3f wo = randomDirection();
    wo[2] = std::abs(wo[2]);
    if (i % 64 == 0)
      wo = Vec3f(0.f, 0.f, 1.f);
    Vec3f wi = randomDirection();
    wi[2] = std::abs(wi[2]);
    put3(in, wo);
    in.push_back(detail::RoughnessToAlpha(std::sqrt(randomAlpha())));
    in.push_back(u01());
    in.push_back(u01());
    put3(in, wi);
  }
  const int n = int(in.size() / 9);
  std::vector<float> out(size_t(n) * 5);
  ispc::kernel_vndf(in.data(), out.data(), n);

  Comparison normal, pdf, pdfHalf;
  for (int i = 0; i < n; i++) {
    const float *p = &in[9 * i];
    const Vec3f wo(p[0], p[1], p[2]), wi(p[6], p[7], p[8]);
    const float a = p[3];
    const Vec3f wm = detail::SampleGGX_VNDF(wo, a, p[4], p[5]);
    normal.add3(&out[5 * i], wm, "point " + std::to_string(i));
    // the pdf of the ISPC sample's own normal, so that a direction a few
    // ulps apart doesn't hide the density's agreement
    const Vec3f wmIspc(out[5 * i], out[5 * i + 1], out[5 * i + 2]);
    pdf.add(out[5 * i + 3],
        detail::PdfGGX_VNDF(wo, wmIspc, a),
        "point " + std::to_string(i));
    pdfHalf.add(out[5 * i + 4],
        detail::PdfGGX_VNDF(wo, (wo + wi).normalized(), a),
        "point " + std::to_string(i));
  }
  check(
      normal.matches(), "GGX visible-normal sampling: " + normal.describe());
  check(pdf.matches() && pdfHalf.matches(),
      "GGX visible-normal pdf: " + pdf.describe()
          + "; of half vectors: " + pdfHalf.describe());
}

void checkValidNormal()
{
  std::vector<float> in;
  int corrected = 0;
  for (int i = 0; i < 8192; i++) {
    const Vec3f Ng = randomDirection();
    // grazing views half the time, so that many need the correction
    Vec3f wo = randomDirection();
    if (wo.dot(Ng) < 0.f)
      wo = -wo;
    if (i % 2)
      wo = (wo - Ng * (0.98f * wo.dot(Ng))).normalized();
    Vec3f n = (Ng + randomDirection() * (0.6f * u01())).normalized();
    if (n.dot(Ng) < 0.f)
      n = -n;
    put3(in, Ng);
    put3(in, wo);
    put3(in, n);
    corrected += detail::EnsureValidSpecularReflection(Ng, wo, n) != n;
  }
  const int n = int(in.size() / 9);
  std::vector<float> out(size_t(n) * 3);
  ispc::kernel_validNormal(in.data(), out.data(), n);

  Comparison c;
  for (int i = 0; i < n; i++) {
    const float *p = &in[9 * i];
    c.add3(&out[3 * i],
        detail::EnsureValidSpecularReflection(Vec3f(p[0], p[1], p[2]),
            Vec3f(p[3], p[4], p[5]),
            Vec3f(p[6], p[7], p[8])),
        "point " + std::to_string(i),
        VALID_NORMAL);
  }
  check(c.matches() && corrected > n / 8,
      "glossy normals raised towards Ng (EnsureValidSpecularReflection(), "
          + std::to_string(corrected) + " corrected): " + c.describe());
}

void checkGlossyLeaf(bool dielectric)
{
  constexpr int LEAF_IN = 26, LEAF_OUT = 12;
  // the clearcoat's F0 (EvalGltfPbr()'s ior 1.5)
  float coatF0 = (1.5f - 1.f) / (1.5f + 1.f);
  coatF0 *= coatF0;
  std::vector<float> in;
  for (int i = 0; i < 16384; i++) {
    const Vec3f Ng = randomDirection();
    Vec3f Ns = i % 4 == 0 ? Ng : (Ng + randomDirection() * 0.3f).normalized();
    Vec3f wo = randomDirection();
    if (wo.dot(Ng) < 0.f && u01() < 0.9f)
      wo = -wo;
    // faced to the incident side, as the integrator hands them over; the
    // lobe's normal sometimes a normal map's, off the smooth one
    const bool front = wo.dot(Ng) > 0.f;
    const Vec3f NgOut = front ? Ng : -Ng;
    const Vec3f NsOut = front ? Ns : -Ns;
    Vec3f lobe = NsOut;
    if (i % 3 == 0) {
      lobe = (NsOut + randomDirection() * 0.4f).normalized();
      if (lobe.dot(NsOut) < 0.f)
        lobe = -lobe;
    }
    const Vec3f n = lobe == NgOut
        ? lobe
        : detail::EnsureValidSpecularReflection(NgOut, wo, lobe);
    Vec3f wi = randomDirection();
    if (wi.dot(n) < 0.f && u01() < 0.8f)
      wi = -wi;
    // half of the directions near the mirror direction, where sharp lobes
    // have their value
    if (i % 2) {
      const Vec3f mirror = n * (2.f * n.dot(wo)) - wo;
      wi = (mirror + randomDirection() * (0.05f * u01())).normalized();
    }
    put3(in, n);
    put3(in, NsOut);
    put3(in, NgOut);
    put3(in, wo);
    for (int k = 0; k < 6; k++)
      in.push_back(dielectric ? coatF0 : u01() * 1.2f);
    in.push_back(randomAlpha());
    put3(in, wi);
    in.push_back(u01());
    in.push_back(u01());
    in.push_back(dielectric ? std::min(u01() * 1.2f, 1.f) : 1.f);
    in.push_back(dielectric ? 1.f : 0.f);
  }
  const int n = int(in.size() / LEAF_IN);
  std::vector<float> out(size_t(n) * LEAF_OUT);
  ispc::kernel_glossyLeaf(in.data(), out.data(), n);

  Comparison eval, pdf, sampleDir, sampleValue, samplePdf;
  int specular = 0, valid = 0, flagsDiffer = 0;
  for (int i = 0; i < n; i++) {
    const float *p = &in[size_t(i) * LEAF_IN];
    const float *q = &out[size_t(i) * LEAF_OUT];
    Bsdf::ClosureTree tree;
    if (dielectric) {
      // EvalGltfPbr()'s clearcoat
      Bsdf::DielectricData data;
      data.weight = p[24];
      data.ior = 1.5f;
      data.roughness = Vec2f(p[18], p[18]);
      data.normal = Vec3f(p[0], p[1], p[2]);
      data.hasShadingNormal = true;
      tree.root = tree.Add(data);
    } else {
      Bsdf::GeneralizedSchlickData data;
      data.normal = Vec3f(p[0], p[1], p[2]);
      data.hasShadingNormal = true;
      data.color0 = Vec3f(p[12], p[13], p[14]);
      data.color82 = Vec3f(1.f);
      data.color90 = Vec3f(p[15], p[16], p[17]);
      data.roughness = Vec2f(p[18], p[18]);
      // EvalGltfPbr()'s lobes have no thin film
      data.thinFilmWeight = 0.f;
      data.thinFilmThickness = 0.f;
      tree.root = tree.Add(data);
    }

    SurfaceInteraction si;
    si.normalShdWldOut = Vec3f(p[3], p[4], p[5]);
    si.normalSrfWldOut = si.normalShdWldOut;
    si.normalGeomWldOut = Vec3f(p[6], p[7], p[8]);
    si.omegaOutWld = Vec3f(p[9], p[10], p[11]);
    const Vec3f wi(p[19], p[20], p[21]);
    const std::string at = "point " + std::to_string(i);
    const float conditioning = ggxConditioning(p[18]);

    eval.add3(q,
        detail::EvalNodeCosine(
            tree, tree.root, si, wi, detail::BumpShadowingContext::Evaluation),
        at,
        conditioning);
    pdf.add(q[3], detail::PdfNode(tree, tree.root, si, wi), at, conditioning);

    const Bsdf::BsdfSample s =
        detail::SampleNode(tree, tree.root, si, p[22], p[23], 0.5f);
    if ((q[11] != 0.f) != s.isSpecular
        || (q[10] > 0.f) != (s.pdfSolidAngle > 0.f)) {
      flagsDiffer++;
      continue;
    }
    specular += s.isSpecular;
    valid += s.pdfSolidAngle > 0.f;
    sampleDir.add3(q + 4, s.omegaInWld, at, conditioning);
    sampleValue.add3(q + 7,
        s.isSpecular ? s.bsdfValue : s.bsdfValueCosine,
        at,
        conditioning);
    samplePdf.add(q[10], s.pdfSolidAngle, at, conditioning);
  }
  const std::string lobe = dielectric ? "a clearcoat (dielectric GGX) leaf"
                                      : "a generalized Schlick GGX leaf";
  check(eval.matches() && pdf.matches(),
      lobe + "'s evaluation: " + eval.describe()
          + "; its pdf: " + pdf.describe());
  check(fewEnough(flagsDiffer, n) && specular > n / 20 && valid > n / 4,
      lobe + " samples (delta and rough) where the C++ one does ("
          + std::to_string(valid) + " valid, " + std::to_string(specular)
          + " delta of " + std::to_string(n) + "; "
          + std::to_string(flagsDiffer) + " differ)");
  check(
      sampleDir.matches() && sampleValue.matches() && samplePdf.matches(),
      lobe + "'s samples: directions " + sampleDir.describe() + "; values "
          + sampleValue.describe() + "; pdfs " + samplePdf.describe());
}

void checkSheenLeaf()
{
  constexpr int SHEEN_IN = 22, SHEEN_OUT = 12;
  std::vector<float> in;
  for (int i = 0; i < 16384; i++) {
    const Vec3f Ng = randomDirection();
    const Vec3f Nsrf = (Ng + randomDirection() * 0.3f).normalized();
    Vec3f wo = randomDirection();
    if (wo.dot(Ng) < 0.f && u01() < 0.9f)
      wo = -wo;
    const bool front = wo.dot(Ng) > 0.f;
    const Vec3f NgOut = front ? Ng : -Ng;
    const Vec3f smooth = front ? Nsrf : -Nsrf;
    // a normal map's normal at a third of the points
    Vec3f n = smooth;
    if (i % 3 == 0) {
      n = (smooth + randomDirection() * 0.4f).normalized();
      if (n.dot(smooth) < 0.f)
        n = -n;
    }
    Vec3f wi = randomDirection();
    if (wi.dot(n) < 0.f && u01() < 0.8f)
      wi = -wi;
    put3(in, n);
    put3(in, smooth);
    put3(in, NgOut);
    put3(in, wo);
    put3(in, Vec3f(u01(), u01(), u01()));
    in.push_back(u01());
    const float r = i % 16 == 0 ? 0.f : u01();
    in.push_back(r * r);
    put3(in, wi);
    in.push_back(u01());
    in.push_back(u01());
  }
  const int n = int(in.size() / SHEEN_IN);
  std::vector<float> out(size_t(n) * SHEEN_OUT);
  ispc::kernel_sheenLeaf(in.data(), out.data(), n);

  Comparison eval, pdf, sample, albedo;
  for (int i = 0; i < n; i++) {
    const float *p = &in[size_t(i) * SHEEN_IN];
    const float *q = &out[size_t(i) * SHEEN_OUT];
    Bsdf::SheenData data;
    data.normal = Vec3f(p[0], p[1], p[2]);
    data.hasShadingNormal = true;
    data.color = Vec3f(p[12], p[13], p[14]);
    data.weight = p[15];
    data.roughness = p[16];
    Bsdf::ClosureTree tree;
    tree.root = tree.Add(data);
    SurfaceInteraction si;
    si.normalShdWldOut = data.normal;
    si.normalSrfWldOut = Vec3f(p[3], p[4], p[5]);
    si.normalGeomWldOut = Vec3f(p[6], p[7], p[8]);
    si.omegaOutWld = Vec3f(p[9], p[10], p[11]);
    const Vec3f wi(p[17], p[18], p[19]);
    const std::string at = "point " + std::to_string(i);

    eval.add3(q,
        detail::EvalNodeCosine(
            tree, tree.root, si, wi, detail::BumpShadowingContext::Evaluation),
        at);
    pdf.add(q[3], detail::PdfNode(tree, tree.root, si, wi), at);
    const Bsdf::BsdfSample s =
        detail::SampleNode(tree, tree.root, si, p[20], p[21], 0.5f);
    sample.add3(q + 4, s.omegaInWld, at);
    sample.add3(q + 7, s.bsdfValueCosine, at);
    sample.add(q[10], s.pdfSolidAngle, at);
    albedo.add(q[11],
        detail::ApproxSheenDirAlbedo(
            std::max(std::abs(data.normal.dot(si.omegaOutWld)), 1e-7f),
            data.roughness),
        at);
  }
  check(eval.matches() && pdf.matches(),
      "a sheen leaf's evaluation: " + eval.describe()
          + "; its pdf: " + pdf.describe());
  check(sample.matches(), "a sheen leaf's samples: " + sample.describe());
  check(albedo.matches(),
      "the sheen's directional albedo fit: " + albedo.describe());
}

void checkTransmissionLeaf()
{
  constexpr int IN = 23, OUT = 14;
  const Vec3f luminance(0.2126f, 0.7152f, 0.0722f);
  std::vector<float> in;
  for (int i = 0; i < 16384; i++) {
    const Vec3f Ng = randomDirection();
    const Vec3f Nsrf =
        i % 4 == 0 ? Ng : (Ng + randomDirection() * 0.3f).normalized();
    // from both sides: back faces refract out of the medium (and reflect
    // totally inside)
    Vec3f wo = randomDirection();
    if (wo.dot(Ng) < 0.f && u01() < 0.5f)
      wo = -wo;
    const bool front = wo.dot(Ng) > 0.f;
    const Vec3f NgOut = front ? Ng : -Ng;
    const Vec3f NsOut = front ? Nsrf : -Nsrf;
    const Vec3f n = NsOut == NgOut
        ? NsOut
        : detail::EnsureValidSpecularReflection(NgOut, wo, NsOut);
    // mostly below the surface, half of those near the refracted direction
    Vec3f wi = randomDirection();
    if (wi.dot(n) > 0.f && u01() < 0.8f)
      wi = -wi;
    const float ior = i % 8 == 0 ? 1.f : 1.f + 1.5f * u01();
    if (i % 2) {
      const float eta = front ? 1.f / ior : ior;
      const float c = n.dot(wo);
      const float k = 1.f - eta * eta * (1.f - c * c);
      if (k > 0.f) {
        const Vec3f t = -eta * wo + (eta * c - std::sqrt(k)) * n;
        wi = (t + randomDirection() * (0.05f * u01())).normalized();
      }
    }
    put3(in, n);
    put3(in, NsOut);
    put3(in, NgOut);
    put3(in, wo);
    put3(in, Vec3f(u01(), u01(), u01()));
    in.push_back(randomAlpha());
    in.push_back(ior);
    in.push_back(front ? 0.f : 1.f);
    put3(in, wi);
    in.push_back(u01());
    in.push_back(u01());
  }
  const int n = int(in.size() / IN);
  std::vector<float> out(size_t(n) * OUT);
  ispc::kernel_transmissionLeaf(
      in.data(), out.data(), n, luminance[0], luminance[1], luminance[2]);

  Comparison eval, pdf, sample;
  int refracted = 0, reflected = 0, flagsDiffer = 0;
  for (int i = 0; i < n; i++) {
    const float *p = &in[size_t(i) * IN];
    const float *q = &out[size_t(i) * OUT];
    // EvalGltfPbr()'s transmission lobe
    Bsdf::DielectricData data;
    data.normal = Vec3f(p[0], p[1], p[2]);
    data.hasShadingNormal = true;
    data.tint = Vec3f(p[12], p[13], p[14]);
    data.roughness = Vec2f(p[15], p[15]);
    data.ior = p[16];
    data.scatterMode = Bsdf::ScatterMode::Transmission;
    Bsdf::ClosureTree tree;
    tree.luminanceCoefficients = luminance;
    tree.root = tree.Add(data);
    SurfaceInteraction si;
    si.normalShdWldOut = Vec3f(p[3], p[4], p[5]);
    si.normalSrfWldOut = si.normalShdWldOut;
    si.normalGeomWldOut = Vec3f(p[6], p[7], p[8]);
    si.omegaOutWld = Vec3f(p[9], p[10], p[11]);
    si.frontFacing = p[17] == 0.f;
    const Vec3f wi(p[18], p[19], p[20]);
    const std::string at = "point " + std::to_string(i);
    const float conditioning = ggxConditioning(p[15]);

    eval.add3(q,
        detail::EvalNodeCosine(
            tree, tree.root, si, wi, detail::BumpShadowingContext::Evaluation),
        at,
        conditioning);
    pdf.add(q[3], detail::PdfNode(tree, tree.root, si, wi), at, conditioning);

    const Bsdf::BsdfSample s =
        detail::SampleNode(tree, tree.root, si, p[21], p[22], 0.5f);
    if ((q[11] != 0.f) != s.isSpecular || (q[12] != 0.f) != s.isTransmission
        || (q[10] > 0.f) != (s.pdfSolidAngle > 0.f)) {
      flagsDiffer++;
      continue;
    }
    refracted += s.pdfSolidAngle > 0.f && s.isTransmission;
    reflected += s.pdfSolidAngle > 0.f && !s.isTransmission;
    sample.add3(q + 4, s.omegaInWld, at, conditioning);
    sample.add3(q + 7,
        s.isSpecular ? s.bsdfValue : s.bsdfValueCosine,
        at,
        conditioning);
    sample.add(q[10], s.pdfSolidAngle, at, conditioning);
    sample.add(q[13], s.eta, at);
  }
  check(eval.matches() && pdf.matches(),
      "a transmission leaf's evaluation: " + eval.describe()
          + "; its pdf: " + pdf.describe());
  check(fewEnough(flagsDiffer, n) && refracted > n / 4 && reflected > 0,
      "a transmission leaf samples refractions (" + std::to_string(refracted)
          + ") and total internal reflections (" + std::to_string(reflected)
          + ") where the C++ one does (" + std::to_string(flagsDiffer)
          + " differ)");
  check(sample.matches(),
      "a transmission leaf's samples: " + sample.describe());
}

void checkReflFrontFilter()
{
  std::vector<float> in;
  // every node of the table's axes, their midpoints, and random points
  for (int k = 0; k <= 64; k++) {
    for (int r = 0; r <= 32; r++) {
      for (int c = 0; c <= 32; c++) {
        if ((k + r + c) % 7)
          continue;
        const float t = float(k) / 64.f;
        in.push_back(float(c) / 30.f);
        in.push_back(float(r) / 30.f);
        in.push_back(1.001f + t * t * (5.f - 1.001f));
      }
    }
  }
  for (int i = 0; i < 8192; i++) {
    in.push_back(u01() * 1.1f - 0.05f);
    in.push_back(u01());
    in.push_back(i % 8 == 0 ? 1.5f : 0.9f + 4.5f * u01());
  }
  const int n = int(in.size() / 3);
  std::vector<float> out(n);
  ispc::kernel_reflFrontFilter(in.data(), out.data(), n);
  Comparison c;
  for (int i = 0; i < n; i++) {
    c.add(out[i],
        detail::LookupBsdlDielectricReflFrontFilter(
            in[3 * i], in[3 * i + 1], in[3 * i + 2]),
        "point " + std::to_string(i));
  }
  check(c.matches(),
      "BSDL's dielectric layer filter from the vendored table: "
          + c.describe());
}

} // namespace

int main()
{
  checkMissingEnergy();
  checkFresnel();
  checkVndf();
  checkValidNormal();
  checkGlossyLeaf(false);
  checkGlossyLeaf(true);
  checkSheenLeaf();
  checkTransmissionLeaf();
  checkReflFrontFilter();
  return g_failures ? 1 : 0;
}
