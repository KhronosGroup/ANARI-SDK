// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0
//
// Exercises object/parameter introspection (SPEC §4.8 "Object and parameter
// introspection") through the core ANARI entry points anariGetObjectSubtypes,
// anariGetObjectInfo and anariGetParameterInfo. No frame is rendered.
//
// The tables are the SDK's vanilla KHR schema plus the bridge's own
// parameters (introspection/hydra_bridge_parameters.json), gated at query
// time on the bound delegate's extension list; this test asserts, per delegate:
//   - every advertised subtype answers a parameter list, and every entry in it
//     answers `required`;
//   - KHR parameters are present iff their extension is claimed (samplers,
//     curve, frame channels), and bridge parameters are always present;
//   - defaults, descriptions, element types, value lists and source
//     extensions come back typed and equal to the KHR schema;
//   - unsupported subtypes/object types, unknown info names and unlisted
//     (name, type) pairs answer nullptr;
//   - returned storage stays valid and stable across queries;
//   - the renderer list keeps its dynamic delegate settings after the bridge
//     parameters.

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

static int g_errors = 0;

static void statusFunc(const void *, ANARIDevice, ANARIObject, ANARIDataType,
    ANARIStatusSeverity severity, ANARIStatusCode, const char *message)
{
  if (severity <= ANARI_SEVERITY_WARNING)
    std::printf("[%s] %s\n",
        severity <= ANARI_SEVERITY_ERROR ? "ERR" : "WARN", message);
  if (severity <= ANARI_SEVERITY_ERROR)
    ++g_errors;
}

static void check(bool ok, const std::string &what)
{
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok)
    ++g_errors;
}

static std::set<std::string> toSet(const char *const *list)
{
  std::set<std::string> s;
  for (; list && *list; ++list)
    s.insert(*list);
  return s;
}

static const ANARIParameter *params(
    ANARIDevice d, ANARIDataType type, const char *subtype)
{
  return (const ANARIParameter *)anariGetObjectInfo(
      d, type, subtype, "parameter", ANARI_PARAMETER_LIST);
}

static bool has(const ANARIParameter *list, const char *name, ANARIDataType t)
{
  for (; list && list->name; ++list)
    if (std::strcmp(list->name, name) == 0 && list->type == t)
      return true;
  return false;
}

static bool hasName(const ANARIParameter *list, const char *name)
{
  for (; list && list->name; ++list)
    if (std::strcmp(list->name, name) == 0)
      return true;
  return false;
}

static bool hasType(const ANARIParameter *list, ANARIDataType t)
{
  for (; list && list->name; ++list)
    if (list->type == t)
      return true;
  return false;
}

static const void *info(ANARIDevice d, ANARIDataType type, const char *subtype,
    const char *param, ANARIDataType ptype, const char *name, ANARIDataType it)
{
  return anariGetParameterInfo(d, type, subtype, param, ptype, name, it);
}

static bool near(float a, float b)
{
  return std::fabs(a - b) < 1e-5f;
}

int main(int argc, char **argv)
{
  const char *delegate = argc > 1 ? argv[1] : "storm";
  const bool isEmbree = std::strcmp(delegate, "embree") == 0;

  ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, nullptr);
  if (!lib)
    return 2;
  ANARIDevice dev = anariNewDevice(lib, delegate);
  if (!dev)
    return 2;

  const std::set<std::string> ext = toSet((const char *const *)anariGetObjectInfo(
      dev, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST));
  std::printf("delegate %s: %zu extensions\n", delegate, ext.size());
  const bool samplers = ext.count("ANARI_KHR_SAMPLER_IMAGE2D") > 0;

  // --- 1. Every advertised subtype answers; every entry answers `required` --
  {
    const ANARIDataType named[] = {ANARI_GEOMETRY, ANARI_MATERIAL, ANARI_SAMPLER,
        ANARI_LIGHT, ANARI_CAMERA, ANARI_RENDERER, ANARI_INSTANCE};
    int subtypes = 0, lists = 0, entries = 0, answered = 0, described = 0;
    for (ANARIDataType t : named) {
      const char **subs = anariGetObjectSubtypes(dev, t);
      for (; subs && *subs; ++subs) {
        ++subtypes;
        const ANARIParameter *p = params(dev, t, *subs);
        if (!p || !has(p, "name", ANARI_STRING))
          continue;
        ++lists;
        if (anariGetObjectInfo(dev, t, *subs, "description", ANARI_STRING))
          ++described;
        for (; p->name; ++p) {
          ++entries;
          const int32_t *req = (const int32_t *)info(
              dev, t, *subs, p->name, p->type, "required", ANARI_BOOL);
          if (req && (*req == 0 || *req == 1))
            ++answered;
        }
      }
    }
    std::printf("  %d subtypes, %d entries\n", subtypes, entries);
    check(subtypes > 0 && lists == subtypes,
        "every advertised subtype answers a parameter list carrying `name`");
    check(entries > 0 && answered == entries,
        "every listed (name, type) entry answers `required` as ANARI_BOOL");
    check(described == subtypes, "every advertised subtype has a description");
    // Anonymous object types answer without a subtype.
    check(has(params(dev, ANARI_FRAME, nullptr), "size", ANARI_UINT32_VEC2)
            && has(params(dev, ANARI_FRAME, nullptr), "world", ANARI_WORLD),
        "frame parameter list (no subtype) has size/world");
    check(has(params(dev, ANARI_SURFACE, "ignored"), "geometry", ANARI_GEOMETRY),
        "surface parameter list ignores the subtype argument");
    check(has(params(dev, ANARI_WORLD, nullptr), "instance", ANARI_ARRAY1D)
            && has(params(dev, ANARI_GROUP, nullptr), "surface", ANARI_ARRAY1D),
        "world/group list their core arrays");
    check(has(params(dev, ANARI_DEVICE, "default"), "statusCallback",
              ANARI_STATUS_CALLBACK),
        "device parameter list has statusCallback");
  }

  // --- 2. Unsupported queries -------------------------------------------------
  check(params(dev, ANARI_GEOMETRY, "notASubtype") == nullptr,
      "unknown geometry subtype -> nullptr");
  // isosurface needs a spatial field, offered only where volumes are (storm).
  check((params(dev, ANARI_GEOMETRY, "isosurface") == nullptr) == isEmbree,
      "isosurface geometry answers iff the delegate offers fields");
  check(params(dev, ANARI_SAMPLER, "image3D") == nullptr,
      "schema-only sampler subtype image3D -> nullptr");
  check(params(dev, ANARI_GEOMETRY, nullptr) == nullptr,
      "named type without a subtype -> nullptr");
  check(params(dev, ANARI_RENDERER, "bogus") == nullptr,
      "unknown renderer subtype -> nullptr");
  // Volumes follow the delegate's volume capability (SPEC §7): storm lists
  // and reflects them, embree lists nothing and answers nullptr.
  const std::set<std::string> volumeSubtypes =
      toSet(anariGetObjectSubtypes(dev, ANARI_VOLUME));
  const std::set<std::string> fieldSubtypes =
      toSet(anariGetObjectSubtypes(dev, ANARI_SPATIAL_FIELD));
  const ANARIParameter *tf = params(dev, ANARI_VOLUME, "transferFunction1D");
  const ANARIParameter *sr =
      params(dev, ANARI_SPATIAL_FIELD, "structuredRegular");
  if (isEmbree) {
    check(volumeSubtypes.empty() && fieldSubtypes.empty() && !tf && !sr,
        "embree: no volume or spatial field subtypes -> nullptr");
  } else {
    auto lists = [](const ANARIParameter *p, const char *name,
                     ANARIDataType type) {
      for (; p && p->name; ++p)
        if (type == p->type && std::strcmp(name, p->name) == 0)
          return true;
      return false;
    };
    // nanovdb is listed iff the device was built with the NanoVDB headers,
    // and then iff its extension is claimed.
    const bool nano = fieldSubtypes.count("nanovdb") != 0;
    std::set<std::string> fields{"structuredRegular"};
    if (nano)
      fields.insert("nanovdb");
    check(volumeSubtypes == std::set<std::string>{"transferFunction1D"}
            && fieldSubtypes == fields,
        "storm lists transferFunction1D and its spatial field subtypes");
    check(nano == bool(ext.count("ANARI_KHR_SPATIAL_FIELD_NANOVDB")),
        "nanovdb listed iff KHR_SPATIAL_FIELD_NANOVDB is claimed");
    if (nano)
      check(lists(params(dev, ANARI_SPATIAL_FIELD, "nanovdb"), "data",
                ANARI_ARRAY1D),
          "storm reflects the nanovdb parameters");
    check(lists(tf, "value", ANARI_SPATIAL_FIELD)
            && lists(tf, "valueRange", ANARI_FLOAT32_BOX1)
            && lists(tf, "color", ANARI_ARRAY1D)
            && lists(tf, "opacity", ANARI_FLOAT32)
            && lists(tf, "unitDistance", ANARI_FLOAT32),
        "storm reflects the transferFunction1D parameters");
    check(lists(sr, "data", ANARI_ARRAY3D) && lists(sr, "origin", ANARI_FLOAT32_VEC3)
            && lists(sr, "spacing", ANARI_FLOAT32_VEC3),
        "storm reflects the structuredRegular parameters");
  }
  check(params(dev, ANARI_VOLUME, nullptr) == nullptr
          && params(dev, ANARI_SPATIAL_FIELD, nullptr) == nullptr
          && params(dev, ANARI_SPATIAL_FIELD, "structuredRectilinear") == nullptr,
      "volume types need a supported subtype -> nullptr");
  check(anariGetObjectInfo(dev, ANARI_GEOMETRY, "triangle", "bogus",
            ANARI_STRING) == nullptr,
      "unknown object info name -> nullptr");
  check(info(dev, ANARI_GEOMETRY, "triangle", "bogus", ANARI_ARRAY1D,
            "description", ANARI_STRING) == nullptr,
      "unknown parameter name -> nullptr");
  check(info(dev, ANARI_GEOMETRY, "triangle", "vertex.position", ANARI_FLOAT32,
            "required", ANARI_BOOL) == nullptr,
      "listed name with an unlisted type -> nullptr");
  check(info(dev, ANARI_GEOMETRY, "sphere", "radius", ANARI_FLOAT32, "default",
            ANARI_FLOAT32_VEC3) == nullptr,
      "default asked with the wrong info type -> nullptr");

  // --- 3. Storage validity and stability -------------------------------------
  {
    const ANARIParameter *a = params(dev, ANARI_GEOMETRY, "triangle");
    const char *firstName = a ? a->name : nullptr;
    (void)params(dev, ANARI_LIGHT, "spot");
    (void)params(dev, ANARI_FRAME, nullptr);
    const ANARIParameter *b = params(dev, ANARI_GEOMETRY, "triangle");
    check(a && a == b, "repeated parameter-list query returns the same storage");
    check(firstName && b && std::strcmp(firstName, b->name) == 0,
        "earlier list contents remain valid after other queries");
    const char *d1 = (const char *)info(dev, ANARI_GEOMETRY, "sphere", "radius",
        ANARI_FLOAT32, "description", ANARI_STRING);
    const char *d2 = (const char *)info(dev, ANARI_GEOMETRY, "sphere", "radius",
        ANARI_FLOAT32, "description", ANARI_STRING);
    check(d1 && d1 == d2, "repeated description query returns the same storage");
  }

  // --- 4. KHR schema answers: defaults, required, element types, values -------
  {
    const float *r = (const float *)info(dev, ANARI_GEOMETRY, "sphere",
        "radius", ANARI_FLOAT32, "default", ANARI_FLOAT32);
    check(r && near(*r, 1.f), "sphere radius default == 1");
    const float *c = (const float *)info(dev, ANARI_MATERIAL, "matte", "color",
        ANARI_FLOAT32_VEC3, "default", ANARI_FLOAT32_VEC3);
    check(c && near(c[0], 0.8f) && near(c[2], 0.8f),
        "matte color default == (0.8, 0.8, 0.8)");
    const float *fovy = (const float *)info(dev, ANARI_CAMERA, "perspective",
        "fovy", ANARI_FLOAT32, "default", ANARI_FLOAT32);
    check(fovy && std::fabs(*fovy - 1.0471975512f) < 1e-4f,
        "perspective fovy default == 60 degrees");
    const float *falloff = (const float *)info(dev, ANARI_LIGHT, "spot",
        "falloffAngle", ANARI_FLOAT32, "default", ANARI_FLOAT32);
    check(falloff && near(*falloff, 0.1f),
        "spot falloffAngle default is the KHR schema's 0.1 (device uses 0; README)");
    const float *xf = (const float *)info(dev, ANARI_INSTANCE, "transform",
        "transform", ANARI_FLOAT32_MAT4, "default", ANARI_FLOAT32_MAT4);
    check(xf && xf[0] == 1.f && xf[1] == 0.f && xf[5] == 1.f && xf[15] == 1.f,
        "instance transform default is the identity");
    const float *bg = (const float *)info(dev, ANARI_RENDERER, "default",
        "background", ANARI_FLOAT32_VEC4, "default", ANARI_FLOAT32_VEC4);
    check(bg && bg[0] == 0.f && bg[3] == 1.f, "renderer background default (0,0,0,1)");

    const int32_t *req = (const int32_t *)info(dev, ANARI_GEOMETRY, "triangle",
        "vertex.position", ANARI_ARRAY1D, "required", ANARI_BOOL);
    const int32_t *opt = (const int32_t *)info(dev, ANARI_GEOMETRY, "triangle",
        "vertex.color", ANARI_ARRAY1D, "required", ANARI_BOOL);
    check(req && *req == 1 && opt && *opt == 0,
        "vertex.position required, vertex.color optional");
    const int32_t *grp = (const int32_t *)info(dev, ANARI_INSTANCE, "transform",
        "group", ANARI_GROUP, "required", ANARI_BOOL);
    const int32_t *geo = (const int32_t *)info(dev, ANARI_SURFACE, nullptr,
        "geometry", ANARI_GEOMETRY, "required", ANARI_BOOL);
    check(grp && *grp == 1 && geo && *geo == 1,
        "instance group and surface geometry are required");

    const ANARIDataType *tri = (const ANARIDataType *)info(dev, ANARI_GEOMETRY,
        "triangle", "primitive.index", ANARI_ARRAY1D, "elementType",
        ANARI_DATA_TYPE_LIST);
    check(tri && tri[0] == ANARI_UINT32_VEC3 && tri[1] == ANARI_UINT64_VEC3
            && tri[2] == ANARI_UNKNOWN,
        "triangle primitive.index elementType == the KHR {UINT32_VEC3, UINT64_VEC3}");
    const std::set<std::string> caps = toSet((const char *const *)info(dev,
        ANARI_GEOMETRY, "cone", "caps", ANARI_STRING, "value", ANARI_STRING_LIST));
    check(caps.count("none") && caps.count("both") && caps.size() == 4,
        "cone caps value list == {none, first, second, both}");
    const ANARIDataType *cv = (const ANARIDataType *)info(dev, ANARI_FRAME,
        nullptr, "channel.color", ANARI_DATA_TYPE, "value", ANARI_DATA_TYPE_LIST);
    std::set<int> colorTypes;
    for (; cv && *cv != ANARI_UNKNOWN; ++cv)
      colorTypes.insert(int(*cv));
    check(colorTypes.count(ANARI_FLOAT32_VEC4)
            && colorTypes.count(ANARI_UFIXED8_RGBA_SRGB) && colorTypes.size() == 3,
        "frame channel.color value list == the three core formats");
    const std::set<std::string> matteAttrs = toSet((const char *const *)info(dev,
        ANARI_MATERIAL, "matte", "color", ANARI_STRING, "value", ANARI_STRING_LIST));
    check(matteAttrs.count("color") && matteAttrs.count("attribute3")
            && matteAttrs.count("worldPosition"),
        "matte color attribute value list is the core attribute set");
    const char *desc = (const char *)info(dev, ANARI_GEOMETRY, "cylinder",
        "primitive.radius", ANARI_ARRAY1D, "description", ANARI_STRING);
    check(desc && desc[0], "cylinder primitive.radius has a description");
    // Vanilla schema entries the device documents as not (fully) honoured
    // are still reflected (README "Object/parameter introspection").
    check(has(params(dev, ANARI_CAMERA, "perspective"), "near", ANARI_FLOAT32)
            && has(params(dev, ANARI_LIGHT, "ring"), "innerRadius", ANARI_FLOAT32)
            && has(params(dev, ANARI_MATERIAL, "matte"), "alphaMode", ANARI_STRING),
        "KHR parameters are listed untrimmed (camera near, ring innerRadius, alphaMode)");
  }

  // --- 5. Bridge (vendor) parameters are present on every delegate ------------
  {
    const float *clip = (const float *)info(dev, ANARI_CAMERA, "orthographic",
        "hydra.clippingRange", ANARI_FLOAT32_VEC2, "default", ANARI_FLOAT32_VEC2);
    check(clip && clip[0] == 0.f && clip[1] == 0.f,
        "hydra.clippingRange default == (0,0) on both cameras");
    const char *clipDesc = (const char *)info(dev, ANARI_CAMERA, "perspective",
        "hydra.clippingRange", ANARI_FLOAT32_VEC2, "description", ANARI_STRING);
    check(clipDesc && std::strstr(clipDesc, "near/far"),
        "hydra.clippingRange description mentions near/far");
    check(info(dev, ANARI_CAMERA, "perspective", "hydra.clippingRange",
              ANARI_FLOAT32_VEC2, "sourceExtension", ANARI_STRING) == nullptr,
        "bridge parameters carry no source extension");
    const ANARIParameter *hdri = params(dev, ANARI_LIGHT, "hdri");
    check(has(hdri, "visible", ANARI_BOOL) && has(hdri, "radiance", ANARI_ARRAY2D),
        "hdri lists the bridge `visible` beside KHR radiance");
    const ANARIParameter *inst = params(dev, ANARI_INSTANCE, "transform");
    check(toSet(anariGetObjectSubtypes(dev, ANARI_INSTANCE))
                == std::set<std::string>{"transform"},
        "instance subtypes == {transform}");
    check(has(inst, "transform", ANARI_FLOAT32_MAT4)
            && has(inst, "transform", ANARI_FLOAT32_MAT3x4)
            && has(inst, "transform", ANARI_ARRAY1D) && has(inst, "id", ANARI_UINT32)
            && has(inst, "id", ANARI_ARRAY1D) && has(inst, "attribute2", ANARI_FLOAT32_VEC2)
            && has(inst, "color", ANARI_ARRAY1D),
        "instance lists the KHR forms plus the bridge's MAT3x4/attribute forms");
    const char *m34 = (const char *)info(dev, ANARI_INSTANCE, "transform",
        "transform", ANARI_FLOAT32_MAT3x4, "sourceExtension", ANARI_STRING);
    const char *a1d = (const char *)info(dev, ANARI_INSTANCE, "transform",
        "transform", ANARI_ARRAY1D, "sourceExtension", ANARI_STRING);
    check(m34 == nullptr && a1d
            && std::strcmp(a1d, "KHR_INSTANCE_TRANSFORM_ARRAY") == 0,
        "each form of a merged name keeps its own source extension (bridge "
        "MAT3x4: none; ARRAY1D: KHR_INSTANCE_TRANSFORM_ARRAY)");
    check(has(params(dev, ANARI_GEOMETRY, "triangle"), "primitive.id", ANARI_ARRAY1D)
            && has(params(dev, ANARI_GEOMETRY, "cone"), "primitive.id", ANARI_ARRAY1D),
        "geometry primitive.id is listed");
    const ANARIParameter *pbr = params(dev, ANARI_MATERIAL, "physicallyBased");
    check(has(pbr, "occlusion", ANARI_SAMPLER) && has(pbr, "occlusion", ANARI_FLOAT32)
            && has(pbr, "occlusion", ANARI_STRING),
        "physicallyBased occlusion lists sampler plus constant/attribute forms");
  }

  // --- 6. Source extensions agree with the advertised list --------------------
  {
    const char *s = (const char *)info(dev, ANARI_GEOMETRY, "sphere", "radius",
        ANARI_FLOAT32, "sourceExtension", ANARI_STRING);
    check(s && std::strcmp(s, "KHR_GEOMETRY_SPHERE") == 0,
        "sphere radius sourceExtension == KHR_GEOMETRY_SPHERE");
    const int32_t *si = (const int32_t *)info(dev, ANARI_GEOMETRY, "sphere",
        "radius", ANARI_FLOAT32, "sourceExtension", ANARI_INT32);
    const char *const *devExt = (const char *const *)anariGetObjectInfo(
        dev, ANARI_DEVICE, "default", "extension", ANARI_STRING_LIST);
    bool indexOk = si && *si >= 0 && size_t(*si) < ext.size()
        && std::strcmp(devExt[*si], "ANARI_KHR_GEOMETRY_SPHERE") == 0;
    check(indexOk, "INT32 sourceExtension indexes the device's own extension list");
    const char *os = (const char *)anariGetObjectInfo(
        dev, ANARI_GEOMETRY, "triangle", "sourceExtension", ANARI_STRING);
    check(os && std::strcmp(os, "KHR_GEOMETRY_TRIANGLE") == 0,
        "triangle object sourceExtension == KHR_GEOMETRY_TRIANGLE");
    const char *bgs = (const char *)info(dev, ANARI_RENDERER, "default",
        "background", ANARI_FLOAT32_VEC4, "sourceExtension", ANARI_STRING);
    check(bgs && std::strcmp(bgs, "KHR_RENDERER_BACKGROUND_COLOR") == 0
            && ext.count("ANARI_KHR_RENDERER_BACKGROUND_COLOR"),
        "renderer background is sourced from the advertised BACKGROUND_COLOR");
    // The ARRAY2D form is the image background, a different extension than
    // the colour form of the same name (needs the ANARI-SDK query generator's
    // per-type sourceExtension, README.md).
    const char *bgi = (const char *)info(dev, ANARI_RENDERER, "default",
        "background", ANARI_ARRAY2D, "sourceExtension", ANARI_STRING);
    check(has(params(dev, ANARI_RENDERER, "default"), "background", ANARI_ARRAY2D)
            && bgi && std::strcmp(bgi, "KHR_RENDERER_BACKGROUND_IMAGE") == 0
            && ext.count("ANARI_KHR_RENDERER_BACKGROUND_IMAGE"),
        "renderer background ARRAY2D is sourced from the advertised "
        "BACKGROUND_IMAGE");
    const char *ambs = (const char *)info(dev, ANARI_RENDERER, "default",
        "ambientRadiance", ANARI_FLOAT32, "sourceExtension", ANARI_STRING);
    check(ambs && std::strcmp(ambs, "KHR_RENDERER_AMBIENT_LIGHT") == 0
            && ext.count("ANARI_KHR_RENDERER_AMBIENT_LIGHT")
            && has(params(dev, ANARI_RENDERER, "default"), "ambientRadiance",
                ANARI_FLOAT32),
        "ambientRadiance is sourced from the advertised AMBIENT_LIGHT");

    // Every reflected subtype's extension is advertised, and every entry's
    // source extension is advertised.
    const ANARIDataType named[] = {ANARI_GEOMETRY, ANARI_MATERIAL, ANARI_SAMPLER,
        ANARI_LIGHT, ANARI_CAMERA, ANARI_INSTANCE};
    bool allAdvertised = true;
    for (ANARIDataType t : named) {
      const char **subs = anariGetObjectSubtypes(dev, t);
      for (; subs && *subs; ++subs) {
        const char *oe = (const char *)anariGetObjectInfo(
            dev, t, *subs, "sourceExtension", ANARI_STRING);
        if (oe && !ext.count(std::string("ANARI_") + oe))
          allAdvertised = false;
        for (const ANARIParameter *p = params(dev, t, *subs); p && p->name; ++p) {
          const char *pe = (const char *)info(
              dev, t, *subs, p->name, p->type, "sourceExtension", ANARI_STRING);
          if (pe && !ext.count(std::string("ANARI_") + pe))
            allAdvertised = false;
        }
      }
    }
    check(allAdvertised,
        "every reflected object/parameter source extension is advertised");
    const std::set<std::string> rendererExt = toSet((const char *const *)
        anariGetObjectInfo(dev, ANARI_RENDERER, "default", "extension",
            ANARI_STRING_LIST));
    check(rendererExt == ext, "renderer 'extension' info matches the device list");
  }

  // --- 7. Frame channels follow the KHR_FRAME_CHANNEL_* claims ----------------
  {
    const std::set<std::string> channels = toSet((const char *const *)
        anariGetObjectInfo(dev, ANARI_FRAME, nullptr, "channel", ANARI_STRING_LIST));
    std::set<std::string> expected{"channel.color", "channel.depth"};
    const std::pair<const char *, const char *> map[] = {
        {"ANARI_KHR_FRAME_CHANNEL_NORMAL", "channel.normal"},
        {"ANARI_KHR_FRAME_CHANNEL_OBJECT_ID", "channel.objectId"},
        {"ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID", "channel.primitiveId"},
        {"ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID", "channel.instanceId"}};
    for (const auto &m : map)
      if (ext.count(m.first))
        expected.insert(m.second);
    check(channels == expected, "frame channel list == core + claimed channels");
    const ANARIParameter *frame = params(dev, ANARI_FRAME, nullptr);
    bool paramsMatch = has(frame, "channel.color", ANARI_DATA_TYPE)
        && has(frame, "channel.depth", ANARI_DATA_TYPE);
    for (const auto &m : map)
      paramsMatch &= has(frame, m.second, ANARI_DATA_TYPE) == bool(ext.count(m.first));
    check(paramsMatch, "frame channel.* parameters present iff claimed");
    check(has(frame, "frameCompletionCallback", ANARI_FRAME_COMPLETION_CALLBACK)
            && has(frame, "frameCompletionCallbackUserData", ANARI_VOID_POINTER)
            && ext.count("ANARI_KHR_FRAME_COMPLETION_CALLBACK"),
        "frame completion callback parameters reflected with their extension");
    check(has(params(dev, ANARI_SURFACE, nullptr), "id", ANARI_UINT32)
            == bool(ext.count("ANARI_KHR_FRAME_CHANNEL_OBJECT_ID")),
        "surface id follows OBJECT_ID");
    check(has(params(dev, ANARI_INSTANCE, "transform"), "id", ANARI_UINT32)
            == bool(ext.count("ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID")),
        "instance id follows INSTANCE_ID");
  }

  // --- 8. Extension-existence gating per delegate -----------------------------
  {
    const ANARIParameter *matte = params(dev, ANARI_MATERIAL, "matte");
    const ANARIParameter *pbr = params(dev, ANARI_MATERIAL, "physicallyBased");
    check(has(matte, "color", ANARI_FLOAT32_VEC3) && has(matte, "opacity", ANARI_FLOAT32)
            && has(matte, "color", ANARI_SAMPLER) && has(pbr, "sheenColor", ANARI_FLOAT32_VEC3),
        "materials list the full KHR schema on every delegate (sampler forms included)");
    const std::set<std::string> subs = toSet(anariGetObjectSubtypes(dev, ANARI_SAMPLER));
    const std::set<std::string> geos = toSet(anariGetObjectSubtypes(dev, ANARI_GEOMETRY));
    if (samplers) {
      check(subs.size() == 4, "sampler extensions claimed: four sampler subtypes");
      const ANARIParameter *img2 = params(dev, ANARI_SAMPLER, "image2D");
      check(has(img2, "image", ANARI_ARRAY2D) && has(img2, "wrapMode2", ANARI_STRING)
              && has(img2, "borderColor", ANARI_FLOAT32_VEC4),
          "image2D lists image/wrapMode2 and the CLAMP_TO_BORDER borderColor");
      const std::set<std::string> wrap = toSet((const char *const *)info(dev,
          ANARI_SAMPLER, "image1D", "wrapMode", ANARI_STRING, "value",
          ANARI_STRING_LIST));
      check(wrap.count("clampToBorder") && wrap.count("mirrorRepeat"),
          "image1D wrapMode values merge CLAMP_TO_BORDER's clampToBorder");
      const char *bcs = (const char *)info(dev, ANARI_SAMPLER, "image2D",
          "borderColor", ANARI_FLOAT32_VEC4, "sourceExtension", ANARI_STRING);
      check(bcs && std::strcmp(bcs, "KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER") == 0,
          "borderColor is sourced from CLAMP_TO_BORDER");
      check(has(params(dev, ANARI_SAMPLER, "primitive"), "inOffset", ANARI_UINT64)
              && has(img2, "inOffset", ANARI_FLOAT32_VEC4),
          "primitive inOffset is UINT64, image inOffset FLOAT32_VEC4");
    } else {
      check(subs.empty(), "sampler extensions unclaimed: no sampler subtypes");
      check(params(dev, ANARI_SAMPLER, "image2D") == nullptr,
          "sampler extensions unclaimed: image2D parameter list -> nullptr");
    }
    if (ext.count("ANARI_KHR_GEOMETRY_CURVE")) {
      check(geos.count("curve")
              && has(params(dev, ANARI_GEOMETRY, "curve"), "vertex.radius", ANARI_ARRAY1D),
          "CURVE claimed: curve is a subtype and lists vertex.radius");
    } else {
      check(!geos.count("curve") && params(dev, ANARI_GEOMETRY, "curve") == nullptr,
          "CURVE unclaimed: curve is neither a subtype nor introspectable");
    }
    check(isEmbree != samplers, "delegate expectation: storm has samplers, embree not");
    check(isEmbree != bool(ext.count("ANARI_KHR_GEOMETRY_CURVE")),
        "delegate expectation: storm has curve, embree not");
    check(hasType(matte, ANARI_SAMPLER) == hasType(pbr, ANARI_SAMPLER),
        "matte and physicallyBased agree on sampler-typed inputs");
  }

  // --- 9. Renderer: bridge parameters then dynamic delegate settings ----------
  {
    const ANARIParameter *ren = params(dev, ANARI_RENDERER, "default");
    check(params(dev, ANARI_RENDERER, nullptr) == ren,
        "renderer queries without a subtype address the single `default`");
    check(has(ren, "pixelSamples", ANARI_INT32) && has(ren, "background", ANARI_FLOAT32_VEC4)
            && has(ren, "ambientColor", ANARI_FLOAT32_VEC3)
            && has(ren, "ambientRadiance", ANARI_FLOAT32),
        "renderer list has the bridge parameters and KHR background/ambient");
    const char *ps = (const char *)info(dev, ANARI_RENDERER, "default",
        "pixelSamples", ANARI_INT32, "description", ANARI_STRING);
    check(ps && std::strstr(ps, "convergedSamplesPerPixel"),
        "pixelSamples description names its Hydra alias");
    // An app seeding parameters from introspection (Vela) must get a usable
    // sample budget, never a zero-filled one (SPEC §4.7).
    const int32_t *psDefault = (const int32_t *)info(dev, ANARI_RENDERER,
        "default", "pixelSamples", ANARI_INT32, "default", ANARI_INT32);
    const int32_t *psMin = (const int32_t *)info(dev, ANARI_RENDERER,
        "default", "pixelSamples", ANARI_INT32, "minimum", ANARI_INT32);
    check(psDefault && *psDefault == 1 && psMin && *psMin == 1,
        "pixelSamples advertises default 1 and minimum 1");
    // A delegate render setting reflected verbatim (SPEC §11.3) after the
    // bridge parameters, with the descriptor's default and never required.
    const char *setting = isEmbree ? "enableSceneColors" : "enableTinyPrimCulling";
    const ANARIDataType settingType = ANARI_BOOL;
    check(has(ren, setting, settingType),
        std::string("renderer list keeps the delegate setting ") + setting);
    int bridgeIdx = -1, settingIdx = -1;
    for (int i = 0; ren && ren[i].name; ++i) {
      if (!std::strcmp(ren[i].name, "pixelSamples"))
        bridgeIdx = i;
      if (!std::strcmp(ren[i].name, setting))
        settingIdx = i;
    }
    check(bridgeIdx >= 0 && settingIdx > bridgeIdx,
        "delegate settings follow the bridge parameters");
    const int32_t *sreq = (const int32_t *)info(dev, ANARI_RENDERER, "default",
        setting, settingType, "required", ANARI_BOOL);
    check(sreq && *sreq == 0 && info(dev, ANARI_RENDERER, "default", setting,
              settingType, "description", ANARI_STRING),
        "delegate setting answers required=false and a description");
    check(!hasName(ren, "convergedSamplesPerPixel"),
        "token aliased by pixelSamples is hidden");
  }

  anariRelease(dev, dev);
  anariUnloadLibrary(lib);
  std::printf("\n%s (%d issues)\n",
      g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", g_errors);
  return g_errors == 0 ? 0 : 1;
}
