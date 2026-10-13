// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Guards the generated normal-mapping surface (scene/NormalMappedSurface.h,
// SPEC §6.5) against OpenUSD drift. Its glslfx copies UsdPreviewSurface's
// parameter list and names previewSurface.glslfx's internal sections, so an
// OpenUSD upgrade can break it in two ways the device can't see:
//
//   1. the parameter list no longer matches Sdr's UsdPreviewSurface node (an
//      added, removed, retyped or re-defaulted input), or the technique no
//      longer matches previewSurface.glslfx's section list;
//   2. the node registers but fails to compile in Storm, which then draws the
//      prim with its fallback shader and only says so through a Tf warning.
//
// Unlike the other tests this one is white-box: part 1 reads Sdr/Hio directly,
// and part 2 renders through the public ANARI API while a TfDiagnosticMgr
// delegate in this process listens for Storm's compile diagnostics (the device
// and the test share OpenUSD's libraries). render_normal_map covers the
// shading itself.

#include "NormalMappedSurface.h"

#include <anari/anari.h>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/tf/diagnosticMgr.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hio/glslfx.h>
#include <pxr/imaging/hio/glslfxConfig.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdr/shaderNode.h>
#include <pxr/usd/sdr/shaderProperty.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

static int g_errors = 0;

static void check(bool ok, const std::string &what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok)
    ++g_errors;
}

// --- part 1: the configuration against OpenUSD's ----------------------------

// The `-- configuration` block of a glslfx source, or "" if it has none.
static std::string ConfigurationOf(const std::string &glslfx)
{
  const std::string tag = "\n-- configuration\n";
  const size_t begin = glslfx.find(tag);
  if (begin == std::string::npos)
    return {};
  const size_t body = begin + tag.size();
  const size_t end = glslfx.find("\n-- ", body);
  return glslfx.substr(body, end == std::string::npos ? end : end - body);
}

static std::vector<std::string> SourceKeys(
    const std::string &glslfx, const char *stage, const std::string &what)
{
  std::string err;
  std::unique_ptr<HioGlslfxConfig> config(HioGlslfxConfig::Read(
      HioGlslfxTokens->defVal, ConfigurationOf(glslfx), what, &err));
  if (!config) {
    check(false, what + ": configuration parses (" + err + ")");
    return {};
  }
  return config->GetSourceKeys(TfToken(stage));
}

static std::string Join(const std::vector<std::string> &v)
{
  return "[" + TfStringJoin(v, ", ") + "]";
}

// Defaults compare as values of the reference's type, so a float/double or
// GfVec3f/GfVec3d spelling difference in the JSON is not drift.
static bool SameDefault(const VtValue &ref, const VtValue &gen)
{
  if (ref == gen)
    return true;
  const VtValue cast = VtValue::CastToTypeOf(gen, ref);
  return !cast.IsEmpty() && cast == ref;
}

static void CheckParameters(
    SdrShaderNodeConstPtr ref, SdrShaderNodeConstPtr gen)
{
  const TfToken tangentSt(anari_hydra::kNormalMappedSurfaceTangentStInput);

  for (const TfToken &name : ref->GetShaderInputNames()) {
    const SdrShaderPropertyConstPtr r = ref->GetShaderInput(name);
    const SdrShaderPropertyConstPtr g = gen->GetShaderInput(name);
    const std::string what = std::string("input '") + name.GetText() + "'";
    if (!g) {
      check(false, what + " of UsdPreviewSurface exists on the generated node");
      continue;
    }
    const SdfValueTypeName rt = r->GetTypeAsSdfType().GetSdfType();
    const SdfValueTypeName gt = g->GetTypeAsSdfType().GetSdfType();
    const VtValue rd = r->GetDefaultValue();
    const VtValue gd = g->GetDefaultValue();
    if (name == TfToken("opacityMode")) {
      // A token in Sdr; Storm turns it into an int for codegen
      // (transparent = 1, presence = 0, hdSt/materialNetwork.cpp), which is
      // what the glslfx declares.
      const int expected = rd == VtValue(TfToken("transparent"))
              || rd == VtValue(std::string("transparent"))
          ? 1
          : 0;
      check(gt.GetType() == TfType::Find<int>() && gd == VtValue(expected),
          what + " is Storm's int encoding of the token default");
      continue;
    }
    // The role (color3f vs normal3f) is lost in glslfx, which spells every
    // vec3 as a color; the value type is what codegen sees.
    check(rt.GetType() == gt.GetType(),
        what + " has the same value type (" + rt.GetAsToken().GetString()
            + " vs " + gt.GetAsToken().GetString() + ")");
    check(SameDefault(rd, gd),
        what + " has the same default (" + TfStringify(rd) + " vs "
            + TfStringify(gd) + ")");
  }

  for (const TfToken &name : gen->GetShaderInputNames()) {
    if (name == tangentSt) {
      const SdrShaderPropertyConstPtr g = gen->GetShaderInput(name);
      check(g->GetTypeAsSdfType().GetSdfType().GetType()
              == TfType::Find<GfVec2f>(),
          std::string("the extra input '") + name.GetText() + "' is a float2");
      continue;
    }
    check(ref->GetShaderInput(name) != nullptr,
        std::string("generated input '") + name.GetText()
            + "' is a UsdPreviewSurface input");
  }
  check(gen->GetShaderInput(tangentSt) != nullptr,
      std::string("the generated node has the extra input '")
          + tangentSt.GetText() + "'");
}

static void CheckTechnique(SdrShaderNodeConstPtr ref)
{
  const std::string path = ref->GetResolvedImplementationURI();
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string upstream = ss.str();
  check(!upstream.empty(), "previewSurface.glslfx is readable at " + path);

  // The surface technique must be upstream's, with only the normal-mapping
  // section swapped; a section added, removed or renamed upstream shows here.
  std::vector<std::string> expected =
      SourceKeys(upstream, "surfaceShader", path);
  bool swapped = false;
  for (std::string &key : expected)
    if (key == "Preview.NormalMapping") {
      key = anari_hydra::kNormalMappedSurfaceSection;
      swapped = true;
    }
  check(swapped, "previewSurface.glslfx's surface technique has "
                 "Preview.NormalMapping");
  const std::vector<std::string> surface = SourceKeys(
      anari_hydra::kNormalMappedSurfaceSource, "surfaceShader", "generated");
  check(surface == expected,
      "the surface technique is upstream's with the normal mapping swapped: "
          + Join(surface) + " vs " + Join(expected));

  const std::vector<std::string> displacement =
      SourceKeys(upstream, "displacementShader", path);
  check(SourceKeys(anari_hydra::kNormalMappedSurfaceSource,
            "displacementShader", "generated")
          == displacement,
      "the displacement technique is upstream's " + Join(displacement));

  // Every named section must exist once the import is resolved.
  std::istringstream gen(anari_hydra::kNormalMappedSurfaceSource);
  HioGlslfx fx(gen);
  std::string reason;
  check(fx.IsValid(&reason) && !fx.GetSurfaceSource().empty(),
      "the generated glslfx resolves every section " + reason);
}

// --- part 2: Storm compiles it ----------------------------------------------

// Collects every Tf diagnostic; Storm reports compile failures as warnings,
// possibly from a render thread.
class DiagnosticLog : public TfDiagnosticMgr::Delegate
{
 public:
  void IssueError(const TfError &e) override
  {
    add("error", e.GetCommentary());
  }
  void IssueFatalError(const TfCallContext &, const std::string &msg) override
  {
    add("fatal", msg);
  }
  void IssueStatus(const TfStatus &) override {}
  void IssueWarning(const TfWarning &w) override
  {
    add("warning", w.GetCommentary());
  }

  std::vector<std::string> take()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::move(m_messages);
  }

 private:
  void add(const char *kind, const std::string &msg)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_messages.push_back(std::string(kind) + ": " + msg);
  }

  std::mutex m_mutex;
  std::vector<std::string> m_messages;
};

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity == ANARI_SEVERITY_WARNING ? "WARN" : "ERROR", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

// Mean luma over the covered (non-background) pixels.
static double renderLuma(ANARIDevice dev, ANARIFrame frame)
{
  anariRenderFrame(dev, frame);
  anariFrameReady(dev, frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType pt = ANARI_UNKNOWN;
  const uint8_t *p = (const uint8_t *)anariMapFrame(
      dev, frame, "channel.color", &w, &h, &pt);
  double sum = 0;
  long covered = 0;
  for (size_t i = 0; p && i < size_t(w) * h; ++i, p += 4)
    if (p[0] > 20 || p[1] > 20 || p[2] > 20) {
      sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
      ++covered;
    }
  anariUnmapFrame(dev, frame, "channel.color");
  check(covered > 1000, "the quad covers the frame");
  return covered ? sum / covered : 0;
}

static void CheckStormCompiles(DiagnosticLog &log)
{
  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  ANARIDevice dev = lib ? anariNewDevice(lib, "storm") : nullptr;
  if (!dev) {
    check(false, "the storm device loads");
    return;
  }

  // One quad facing +Z with st spanning the unit square, and a tangent-space
  // normal tilted toward -X, away from the light, in attribute1: N.L drops
  // from 0.8 to 0.28.
  float verts[12] = {-0.9f, -0.9f, 0, 0.9f, -0.9f, 0, 0.9f, 0.9f, 0, -0.9f,
      0.9f, 0};
  float st[8] = {0, 0, 1, 0, 1, 1, 0, 1};
  float tilt[12];
  for (int v = 0; v < 4; ++v) {
    tilt[3 * v + 0] = -0.6f;
    tilt[3 * v + 1] = 0.f;
    tilt[3 * v + 2] = 0.8f;
  }
  ANARIArray1D pos =
      anariNewArray1D(dev, verts, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIArray1D attr0 =
      anariNewArray1D(dev, st, nullptr, nullptr, ANARI_FLOAT32_VEC2, 4);
  ANARIArray1D attr1 =
      anariNewArray1D(dev, tilt, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIGeometry g = anariNewGeometry(dev, "quad");
  anariSetParameter(dev, g, "vertex.position", ANARI_ARRAY1D, &pos);
  anariSetParameter(dev, g, "vertex.attribute0", ANARI_ARRAY1D, &attr0);
  anariSetParameter(dev, g, "vertex.attribute1", ANARI_ARRAY1D, &attr1);
  anariCommitParameters(dev, g);

  ANARIMaterial mat = anariNewMaterial(dev, "physicallyBased");
  float white[3] = {1, 1, 1};
  float zero = 0, one = 1;
  anariSetParameter(dev, mat, "baseColor", ANARI_FLOAT32_VEC3, white);
  anariSetParameter(dev, mat, "metallic", ANARI_FLOAT32, &zero);
  anariSetParameter(dev, mat, "roughness", ANARI_FLOAT32, &one);
  anariCommitParameters(dev, mat);

  ANARISurface surf = anariNewSurface(dev);
  anariSetParameter(dev, surf, "geometry", ANARI_GEOMETRY, &g);
  anariSetParameter(dev, surf, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(dev, surf);

  ANARILight light = anariNewLight(dev, "directional");
  float ldir[3] = {-0.6f, 0, -0.8f};
  float irr = 1.5f;
  anariSetParameter(dev, light, "direction", ANARI_FLOAT32_VEC3, ldir);
  anariSetParameter(dev, light, "irradiance", ANARI_FLOAT32, &irr);
  anariCommitParameters(dev, light);

  ANARIWorld world = anariNewWorld(dev);
  ANARIArray1D surfs =
      anariNewArray1D(dev, &surf, nullptr, nullptr, ANARI_SURFACE, 1);
  ANARIArray1D lights =
      anariNewArray1D(dev, &light, nullptr, nullptr, ANARI_LIGHT, 1);
  anariSetParameter(dev, world, "surface", ANARI_ARRAY1D, &surfs);
  anariSetParameter(dev, world, "light", ANARI_ARRAY1D, &lights);
  anariCommitParameters(dev, world);

  ANARICamera cam = anariNewCamera(dev, "perspective");
  float eye[3] = {0, 0, 3}, dir[3] = {0, 0, -1}, up[3] = {0, 1, 0};
  float fovy = 0.6f;
  anariSetParameter(dev, cam, "position", ANARI_FLOAT32_VEC3, eye);
  anariSetParameter(dev, cam, "direction", ANARI_FLOAT32_VEC3, dir);
  anariSetParameter(dev, cam, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(dev, cam, "fovy", ANARI_FLOAT32, &fovy);
  anariCommitParameters(dev, cam);

  ANARIRenderer ren = anariNewRenderer(dev, "default");
  anariCommitParameters(dev, ren);

  ANARIFrame frame = anariNewFrame(dev);
  uint32_t size[2] = {128, 128};
  ANARIDataType colorFmt = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(dev, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(dev, frame, "channel.color", ANARI_DATA_TYPE, &colorFmt);
  anariSetParameter(dev, frame, "world", ANARI_WORLD, &world);
  anariSetParameter(dev, frame, "camera", ANARI_CAMERA, &cam);
  anariSetParameter(dev, frame, "renderer", ANARI_RENDERER, &ren);
  anariCommitParameters(dev, frame);

  const double flat = renderLuma(dev, frame);
  for (const std::string &m : log.take())
    std::printf("  (flat render) %s\n", m.c_str());

  // A transform sampler on `normal` selects the generated surface.
  ANARISampler xf = anariNewSampler(dev, "transform");
  anariSetParameter(dev, xf, "inAttribute", ANARI_STRING, "attribute1");
  anariCommitParameters(dev, xf);
  anariSetParameter(dev, mat, "normal", ANARI_SAMPLER, &xf);
  anariCommitParameters(dev, mat);
  const double mapped = renderLuma(dev, frame);
  const std::vector<std::string> diags = log.take();
  for (const std::string &m : diags)
    std::printf("  (normal-mapped render) %s\n", m.c_str());
  std::printf("  luma: flat %.1f, normal-mapped %.1f\n", flat, mapped);

  check(diags.empty(),
      "Storm compiles the generated surface without a diagnostic");
  // After a compile failure Storm draws the prim with a fallback shader,
  // which renders brighter than the flat reference here, not darker.
  check(mapped < flat - 25.0,
      "the generated surface shades (the -X tilt darkens the quad)");

  anariRelease(dev, xf);
  anariRelease(dev, frame);
  anariRelease(dev, ren);
  anariRelease(dev, cam);
  anariRelease(dev, world);
  anariRelease(dev, lights);
  anariRelease(dev, surfs);
  anariRelease(dev, light);
  anariRelease(dev, surf);
  anariRelease(dev, mat);
  anariRelease(dev, g);
  anariRelease(dev, attr1);
  anariRelease(dev, attr0);
  anariRelease(dev, pos);
  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
}

int main()
{
  DiagnosticLog log;
  TfDiagnosticMgr::GetInstance().AddDelegate(&log);

  std::printf("parameters and technique against OpenUSD\n");
  SdrRegistry &reg = SdrRegistry::GetInstance();
  SdrShaderNodeConstPtr ref = reg.GetShaderNodeByIdentifierAndType(
      TfToken("UsdPreviewSurface"), HioGlslfxTokens->glslfx);
  SdrShaderNodeConstPtr gen = reg.GetShaderNodeFromSourceCode(
      anari_hydra::kNormalMappedSurfaceSource, HioGlslfxTokens->glslfx,
      SdrTokenMap());
  check(ref && ref->IsValid(), "Sdr has a glslfx UsdPreviewSurface node");
  check(gen && gen->IsValid(), "the generated glslfx registers with Sdr");
  if (ref && gen) {
    CheckParameters(ref, gen);
    CheckTechnique(ref);
  }
  const std::vector<std::string> diags = log.take();
  for (const std::string &m : diags)
    std::printf("  (registration) %s\n", m.c_str());
  check(diags.empty(), "registration and parsing raise no diagnostic");

  std::printf("Storm compiles the generated surface\n");
  CheckStormCompiles(log);

  TfDiagnosticMgr::GetInstance().RemoveDelegate(&log);
  std::printf(g_errors ? "\nCHECKS FAILED (%d issues)\n" : "\nALL CHECKS PASSED\n",
      g_errors);
  return g_errors ? 1 : 0;
}
