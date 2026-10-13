// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Non-GL query test: exercise the per-delegate extension truth (SPEC §4.8).
// The device computes its reported ANARI extension list per selected delegate
// and honestly omits what that delegate cannot do. Storm publishes
// basisCurves / a material sprim (so curve, samplers stay); HdEmbree
// publishes neither (so they drop) but keeps mesh geometry, sphere, cylinder,
// lights and materials. No frame is created and nothing is rendered.

#include <anari/anari.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

static int g_errors = 0;

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERROR" : "WARN",
        message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const char *what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_errors;
}

static std::set<std::string> toSet(const char *const *list)
{
  std::set<std::string> s;
  for (const char *const *p = list; p && *p; ++p)
    s.insert(*p);
  return s;
}

static bool has(const std::set<std::string> &s, const char *k)
{
  return s.find(k) != s.end();
}

int main(int argc, char **argv)
{
  const char *subtype = argc > 1 ? argv[1] : "storm";
  const bool isEmbree = std::strcmp(subtype, "embree") == 0;

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib) {
    std::printf("FAIL: could not load anari library 'hydra'\n");
    return 2;
  }

  // --- library-level extension list (probes the delegate) -------------------
  std::set<std::string> ext = toSet(anariGetDeviceExtensions(lib, subtype));
  std::printf("library extensions (%s): %zu\n", subtype, ext.size());

  // --- device-level "extension" property must agree -------------------------
  ANARIDevice dev = anariNewDevice(lib, subtype);
  check(dev != nullptr, "device created for delegate subtype");
  if (!dev)
    return 2;

  const char *const *devList = nullptr;
  int got = anariGetProperty(dev,
      dev,
      "extension",
      ANARI_STRING_LIST,
      &devList,
      sizeof(devList),
      ANARI_WAIT);
  check(got == 1 && devList != nullptr, "device 'extension' property returned");
  std::set<std::string> devExt = toSet(devList);
  check(devExt == ext, "device extension property matches library list");

  // --- object-info reflection must serve the same list ----------------------
  // Consumers reach the extension list through anariGetObjectInfo too (vsr's
  // AnariHandleCache dereferences the result without a null check).
  const char *const *infoList = (const char *const *)anariGetObjectInfo(
      dev, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST);
  check(infoList != nullptr, "device object-info 'extension' returned");
  check(infoList && toSet(infoList) == ext,
      "object-info extension list matches library list");

  // --- geometry / sampler subtypes reflect the same capability gating -------
  std::set<std::string> geo =
      toSet(anariGetObjectSubtypes(dev, ANARI_GEOMETRY));
  std::set<std::string> smp = toSet(anariGetObjectSubtypes(dev, ANARI_SAMPLER));

  // --- assertions -----------------------------------------------------------
  // Unconditional surface: mesh-backed geometry, materials, lights.
  check(has(ext, "ANARI_KHR_GEOMETRY_TRIANGLE"), "TRIANGLE present");
  check(has(ext, "ANARI_KHR_GEOMETRY_CYLINDER"), "CYLINDER present");
  check(has(ext, "ANARI_KHR_MATERIAL_MATTE"), "MATERIAL_MATTE present");
  check(has(ext, "ANARI_KHR_LIGHT_DIRECTIONAL"), "LIGHT_DIRECTIONAL present");
  // The hdri radiance image reaches HdEmbree too (it reads it through Hio as
  // linear float RGB), so the dome-light gate claims it on both.
  check(has(ext, "ANARI_KHR_LIGHT_HDRI"), "LIGHT_HDRI present");

  // Standard AOV frame channels (SPEC §11.4/§4.8): each KHR_FRAME_CHANNEL_*
  // extension is claimed iff the delegate offers a valid default descriptor for
  // its Hydra AOV. Depth and the three ID buffers are unconditional across both
  // reference delegates; normal accepts either the `normal` AOV (HdEmbree) or
  // the eye-space `Neye` AOV (HdStorm), so both claim it.
  check(
      has(ext, "ANARI_KHR_FRAME_CHANNEL_COLOR"), "FRAME_CHANNEL_COLOR present");
  check(
      has(ext, "ANARI_KHR_FRAME_CHANNEL_DEPTH"), "FRAME_CHANNEL_DEPTH present");
  check(has(ext, "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID"),
      "FRAME_CHANNEL_OBJECT_ID present");
  check(has(ext, "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID"),
      "FRAME_CHANNEL_PRIMITIVE_ID present");
  check(has(ext, "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID"),
      "FRAME_CHANNEL_INSTANCE_ID present");

  check(has(ext, "ANARI_KHR_FRAME_CHANNEL_NORMAL"),
      "FRAME_CHANNEL_NORMAL present (normal or Neye AOV)");

  // ANARI_HYDRA_FRAME_AOV (arbitrary named AOVs, SPEC §11.4) is claimed by any
  // delegate that publishes the `renderBuffer` bprim — both reference delegates
  // do, so both gain it.
  check(has(ext, "ANARI_HYDRA_FRAME_AOV"), "FRAME_AOV present");

  // Completion callbacks fire from the device worker after every render job,
  // whatever the delegate (SPEC §8.7).
  check(has(ext, "ANARI_KHR_FRAME_COMPLETION_CALLBACK"),
      "FRAME_COMPLETION_CALLBACK present");

  check(ext.count("ANARI_KHR_CAMERA_ORTHOGRAPHIC"), "ORTHOGRAPHIC present");
  // Sharp per-delegate totals: the honest lists (SPEC §4.8) sum to fixed
  // counts. storm adds all five AOV channels atop its fuller geometry/sampler
  // surface (image1D/2D, primitive, transform, clamp-to-border), plus
  // FRAME_AOV, orthographic cameras, the two instance transform extensions
  // and the renderer background colour (33); embree adds all five AOV
  // channels atop its narrower surface, plus FRAME_AOV, tessellated spheres,
  // orthographic cameras, the two instance transform extensions and the
  // renderer background colour (27). Both also claim the delegate-independent
  // frame completion callback and renderer background image (+2: 35 / 29),
  // and both have dome lights, so both claim the ambient fill (+1: 36 / 30).
  // storm alone offers spatial fields: structuredRegular fields,
  // transferFunction1D volumes and isosurfaces extracted from those fields
  // (+3: 39), and nanovdb fields when the device was built with the NanoVDB
  // headers (+1).
  const size_t nano = has(ext, "ANARI_KHR_SPATIAL_FIELD_NANOVDB") ? 1u : 0u;
  check(ext.size() == (isEmbree ? 30u : 39u + nano),
      "extension count matches delegate");
  // The `default` renderer's background is the frame's clear colour on every
  // delegate, and its ambient fill is a synthesized dome light, claimed
  // wherever the delegate has dome lights (SPEC §4.7).
  check(has(ext, "ANARI_KHR_RENDERER_BACKGROUND_COLOR"),
      "RENDERER_BACKGROUND_COLOR present");
  check(has(ext, "ANARI_KHR_RENDERER_BACKGROUND_IMAGE"),
      "RENDERER_BACKGROUND_IMAGE present");
  check(has(ext, "ANARI_KHR_RENDERER_AMBIENT_LIGHT"),
      "RENDERER_AMBIENT_LIGHT present");
  check(has(ext, "ANARI_KHR_INSTANCE_TRANSFORM"), "INSTANCE_TRANSFORM present");
  check(has(ext, "ANARI_KHR_INSTANCE_TRANSFORM_ARRAY"),
      "INSTANCE_TRANSFORM_ARRAY present");

  if (isEmbree) {
    // HdEmbree: no points/basisCurves rprims, no material sprim.
    check(has(ext, "ANARI_KHR_GEOMETRY_SPHERE"), "embree includes SPHERE");
    check(!has(ext, "ANARI_KHR_GEOMETRY_CURVE"), "embree omits CURVE");
    check(
        !has(ext, "ANARI_KHR_SAMPLER_IMAGE1D"), "embree omits SAMPLER_IMAGE1D");
    check(
        !has(ext, "ANARI_KHR_SAMPLER_IMAGE2D"), "embree omits SAMPLER_IMAGE2D");
    check(!has(ext, "ANARI_KHR_SAMPLER_PRIMITIVE"),
        "embree omits SAMPLER_PRIMITIVE");
    check(!has(ext, "ANARI_KHR_SAMPLER_TRANSFORM"),
        "embree omits SAMPLER_TRANSFORM");
    check(!has(ext, "ANARI_KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER"),
        "embree omits SAMPLER_IMAGExD_CLAMP_TO_BORDER");
    check(geo.find("sphere") != geo.end(), "embree geometry includes 'sphere'");
    check(geo.find("curve") == geo.end(), "embree geometry omits 'curve'");
    check(smp.empty(), "embree publishes no sampler subtypes");
    check(!has(ext, "ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
            && !has(ext, "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D"),
        "embree omits the volume extensions (no volume rprim)");
    check(!has(ext, "ANARI_KHR_GEOMETRY_ISOSURFACE")
            && geo.find("isosurface") == geo.end(),
        "embree omits isosurfaces (no spatial field subtypes)");
  } else {
    // HdStorm: full geometry + sampler surface.
    check(has(ext, "ANARI_KHR_GEOMETRY_SPHERE"), "storm includes SPHERE");
    check(has(ext, "ANARI_KHR_GEOMETRY_CURVE"), "storm includes CURVE");
    check(has(ext, "ANARI_KHR_SAMPLER_IMAGE2D"),
        "storm includes SAMPLER_IMAGE2D");
    check(has(ext, "ANARI_KHR_SAMPLER_PRIMITIVE"),
        "storm includes SAMPLER_PRIMITIVE");
    check(has(ext, "ANARI_KHR_SAMPLER_TRANSFORM"),
        "storm includes SAMPLER_TRANSFORM");
    check(has(ext, "ANARI_KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER"),
        "storm includes SAMPLER_IMAGExD_CLAMP_TO_BORDER");
    check(geo.find("sphere") != geo.end(), "storm geometry includes 'sphere'");
    check(geo.find("curve") != geo.end(), "storm geometry includes 'curve'");
    check(smp
            == std::set<std::string>{"image1D",
                "image2D",
                "primitive",
                "transform"},
        "storm sampler subtypes match the claimed extensions");
    check(has(ext, "ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR")
            && has(ext, "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D"),
        "storm includes structuredRegular fields + transferFunction1D volumes");
    check(has(ext, "ANARI_KHR_GEOMETRY_ISOSURFACE")
            && geo.find("isosurface") != geo.end(),
        "storm includes isosurface geometry");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);

  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
      g_errors);
  return g_errors == 0 ? 0 : 1;
}
