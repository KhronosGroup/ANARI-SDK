---
status: accepted
---
# MaterialX documents become MaterialXCpp graphs through the MaterialX library, keeping the native material models

> **Tracker references.** This record predates halcyon's move into VisRTX: its ticket
> and follow-up numbers ("pathtracer ticket N", "follow-up N") refer to halcyon's
> original private tracker, and its commit hashes to the deprecated `anari-halcyon`
> repository (see AGENTS.md, Overview).

The `materialx` material subtype (pathtracer ticket 22) follows VisRTX's contract
(`VISRTX_MATERIAL_MATERIALX`), declared as halcyon's `HALCYON_MATERIAL_MATERIALX` with the
same parameter names (decision D, below): the app hands over a `.mtlx` document (`sourceType`
`documentFile` or `documentInline`, `source`), picks a material in it (`materialName`), and
overrides shader inputs by name with values or image2D samplers. Typhoon never reads such a
document: OpenUSD's usdMtlx turns `.mtlx` files into UsdShade, Hydra flattens that into an
`HdMaterialNetwork2`, and `mxcppAdapter.cpp` renames a few node types on the way to
`mxcpp::MaterialGraph`. Halcyon has to do the document-to-graph step itself.

This note records how (research ticket 21), with evidence, and the task breakdown for 22.
Ticket numbers are those of the pathtracer series; "Typhoon (01)" is the Typhoon reference
build of ticket 01, the image oracle.
Citations: VisRTX at `53e20fb6` (`rtx/` = `devices/rtx/`); MaterialX 1.39.6 at `38368ee0`
(`~/src/MaterialX`, paths under `source/`); Typhoon at the pin `70c45e83b`
(`TY/` = `pxr/imaging/plugin/hdEmbree/`); halcyon's vendored copy of MaterialXCpp is
`MXCPP/` = `external/typhoon/src/renderer/materials/MaterialXCpp/`.

## Decision

Halcyon links MaterialXCore and MaterialXFormat (the XML reader; no shader generators) and
converts the selected material's shader node into an `mxcpp::MaterialGraph` itself:

1. **Read** the document with `readFromXmlFile()` (its directory joins the search path for
   XIncludes) or `readFromXmlString()`, against a standard data library loaded once per
   process (as implemented: embedded in the device) and shared read-only through `Document::setDataLibrary()`. Parse errors
   (`ExceptionParseError`, `ExceptionFileMissing`) are an ERROR and make the material
   invalid, as do an unknown `materialName` and a graph that doesn't compile, so its
   surfaces show `invalidMaterialColor` in every renderer (as implemented; this note first
   said the pathtracer would shade it as a surface without a graph). `Document::validate()`
   failures are a WARNING.
2. **Select** material nodes (`getMaterialNodes()`) with a surface shader
   (`mx::getShaderNodes()`; as implemented, or a volume shader, whose material alone is a
   volume-only boundary). `materialName` matches `getNamePath()` or `getName()`; empty
   means the first, as VisRTX does. The `materialNames` property lists their name paths.
3. **Flatten only what MaterialXCpp lacks.** Call `flattenSubgraphs()` on the document and on
   each of its own node graphs with a filter that keeps every node MaterialXCpp evaluates
   natively: registered nodes (`NodeRegistry::Find()`) and the material models graph
   compilation dispatches itself (standard_surface, open_pbr_surface, gltf_pbr,
   UsdPreviewSurface, disney_principled, ...). A one-node probe compile
   (`EvalGraph::Compile()` of the node as the surface terminal) tells the models apart
   through the public API, so the list follows the pin. Nodes MaterialXCpp lacks but
   MaterialX implements as node graphs are inlined down to nodes it has.
4. **Convert** from the shader node upstream, mirroring `mxcppAdapter.cpp`'s output:
   - node keys are element name paths; `nodeTypeId` is `getNodeDef()->getName()` (the
     `ND_*` id, version-resolved);
   - connections through `nodename`/`output`, `nodegraph`/`output` and `interfacename`
     resolve to the driving node and its output name (`Input::getConnectedNode()`,
     `getConnectedOutput()`, `getInterfaceInput()`);
   - Typhoon's `ND_UsdPrimvarReader_*` and `ND_UsdTransform2d` renames apply unchanged
     (they are MaterialX nodedefs, in `bxdf/usd_preview_surface.mtlx`);
   - values become `mxcpp::Value` as `_ConvertValue()` makes them (color3 and vector3 are both
     `Vec3f`; matrices row by row);
   - `filename` inputs are resolved with the element's string resolver (`fileprefix`,
     tokens) and, if still relative, against the document's directory (inline documents:
     the working directory), into absolute paths;
   - colour spaces (below) become the `colorSpace:<input>` parameters and converted
     constants Typhoon's graphs expect;
   - terminals point at the shader node: `surface` from a surface shader, `volume` from a volume shader
     (MaterialXCpp wraps it into an interior medium), `displacement` from an
     `ND_displacement_float` shader, which halcyon compiles but doesn't use (no
     displacement: ticket 27 put it out of scope; a DEBUG message says so).
5. **Override** inputs after conversion, on the graph, without re-reading the document:
   - a value parameter replaces the shader node input of that name (VisRTX's leaf names:
     `base_color`); types FLOAT32, FLOAT32_VEC2/3/4, INT32, BOOL as the input's type takes
     them; a STRING on a `filename` input replaces the path (as implemented: also STRING on
     string inputs, and a sampler or an attribute name on float, vector2/3/4 and color3/4
     inputs, through the sampler and geomprop nodes; an attribute the hit lacks gives the
     node's default since pathtracer ticket 50);
   - a sampler on a `filename` input (named by its path, `image1/file`, as listed in the
     `textureInputs` property) replaces the file with the key `anari:sampler/<index>`, so the
     document's own image node samples it through `Sample2D()` (ADR 0005), with its own
     texture coordinates;
   - a sampler on any other shader input disconnects that input and connects halcyon's
     sampler node (`halcyon_sampler_vector4`, ADR 0005) through the component nodes the
     input's type needs, exactly as physicallyBased parameters are built
     (`MaterialConverter.cpp`, whose `GraphEditor` does it on a document's graph); the old
     value is the sampler's default; an attribute name (a STRING naming `attribute0`,
     `color`, ...) on such an input connects the attribute's geomprop node the same way;
   - a name that is neither warns, as VisRTX's MDL path does.
6. **Compile** with `CppMaterial::compile()` as the other subtypes do.

File textures go through the design in ADR 0005 ("File-backed textures"), with the loader
of decision B: stb_image plus tinyexr and a small ASCII PNM reader
(`renderer/pathtracer/ImageFile.*`).

## Answers

### 1. How VisRTX implements it

- **No graph interpretation: MaterialX → MDL.** `MaterialX` derives from `MDL`
  (`rtx/device/material/MaterialX.h:44`); the transcoder
  (`rtx/device/materialx/Transcoder.cpp`, compiled into the device,
  `rtx/device/CMakeLists.txt:228-231`; there is no `visrtx_mtlx` target) generates MDL 1.7
  with MaterialXGenMdl (`Transcoder.cpp:414-438`) and hands it to the MDL compiler as code
  (`MaterialX.cpp:297-304`, `MDL.cpp:253-260`).
- **Library:** `find_package(MaterialX 1.39 REQUIRED)`, out of tree only, because the target
  names collide with USD's copy (`rtx/CMakeLists.txt:203-208`); links Core, Format, GenShader,
  GenMdl (`rtx/device/CMakeLists.txt:290-293`). Optional: `VISRTX_ENABLE_MATERIALX_SUPPORT`,
  default OFF, only with MDL (`rtx/CMakeLists.txt:80-86`); the extension JSON is added to the
  device definitions only then (`rtx/device/CMakeLists.txt:411-421`, `:434-440`).
- **APIs:** `createDocument`, `loadLibraries({"libraries"}, ...)` (`Transcoder.cpp:72-85`),
  `readFromXmlString`/`readFromXmlFile` then `importLibrary` (`:185-199`),
  `findRenderableElements` (from MaterialXGenShader; `:308`, `:342`, `:364`). No
  `flattenSubgraphs`, `getShaderNodes` or string resolvers: MDL generation does all of that.
- **`sourceType`**: `documentFile` (default) or `documentInline`; anything else warns and uses
  `documentFile` (`MaterialX.cpp:76-94`). File modification times are watched and trigger a
  re-transcode (`:128-135`).
- **`materialName`**: `getNamePath()` or `getName()` match; unset or `""` takes the first
  renderable element (`Transcoder.cpp:302-321`, `MaterialX.cpp:105-113`). Without any
  material, VisRTX instantiates a surface-shader nodedef authored in the document
  (`Transcoder.cpp:240-300`), an extension the spec doesn't mention.
- **Errors**: exceptions become "MaterialX: failed to transcode" (ERROR) plus a fallback to a
  white diffuse MDL material (`MaterialX.cpp:180-184`, `MDL.cpp:228`).
- **`materialNames`**: name paths of renderable elements minus nodedef implementation outputs
  (`Transcoder.cpp:212-219`, `:363-366`). **`textureInputs`**: the `filename`-typed inputs of
  the document's nodes, as `node/input` paths (`Transcoder.cpp:476-490`). Both are
  `ANARI_STRING_LIST` and absent until a successful commit (`MaterialX.cpp:316-345`).
- **Overrides by name**: every MDL argument maps from a shader-node input path, by its leaf
  name (`Transcoder.cpp:439-492`); values must match the type exactly
  (`MDL.cpp:420-490`); unknown names warn (`MDL.cpp:383-387`).
- **Sampler-bound inputs**: only image2D (and VisRTX's compressed image2D) samplers
  (`MaterialX.cpp:48-52`, `:240-247`). On a `filename` input the sampler replaces the file
  (`MaterialX.cpp:249`); on a constant input VisRTX splices an `<image>` node in front of
  it and re-transcodes (`Transcoder.cpp:371-412`). The sampler's `inTransform`/`outTransform`
  apply (`rtx/device/gpu/evalMaterialParameters.h:295-321`), its `inAttribute` doesn't: MDL
  texture coordinate 0 is `attribute0` (`rtx/device/material/shaders/MDLShader_ptx.cu:95-105`).
- **Files**: stb_image (plus a DDS loader), through the MDL resolver
  (`rtx/device/mdl/SamplerRegistry.cpp:339-421`); relative paths are probably not resolved
  against the document (the module is compiled from a string, `MaterialRegistry.cpp:354-372`;
  untested in VisRTX). Colour spaces: only what MaterialXGenMdl's default colour management
  emits (`Transcoder.cpp:415-417`).
- **Displacement / volume**: not compiled; of the volume only the scattering coefficient is
  used (`rtx/libmdl/Core.cpp:515-529`).
- **Standard library location**: device parameter `materialxSearchPaths`, then
  `MATERIALX_SEARCH_PATH`, then `getDefaultDataSearchPath()`, then a path baked at build time
  (`Transcoder.cpp:99-166`; VisRTX's
  `docs/adr/0008-materialx-distribution-documents-resolve-at-runtime.md`), reported as
  `materialx.distributionRoot`.

Halcyon takes the contract (parameters, properties, the image2D-sampler rule for `filename`
inputs), not the implementation: there is no MDL here, and MaterialXCpp interprets the graph.

### 2. From a document to a `MaterialGraph`

- **Typhoon's input is already flat.** UsdImaging flattens node graphs, so
  `ConvertHdNetworkToMxcppGraph()` (`TY/renderer/materials/mxcppAdapter.cpp:328-396`) only
  renames USD node types (`:61-104`), their inputs (`:106-134`) and outputs (`:136-152`),
  converts values (`:154-250`) and converts constant colours that carry `colorSpace:<input>`
  (`:261-325`). It has no node-graph, `fileprefix` or document-colour-space handling.
- **MaterialXCpp node ids are MaterialX nodedef names** (registered by the `Register*Nodes()`
  functions in `MXCPP/nodes/*.cpp`, which `MXCPP/nodeRegistry.cpp:114-143` runs; about 780 of
  them). Any reachable unknown id makes the whole terminal Invalid, with no per-node fallback
  (`MXCPP/graph.cpp:693-698`).
- **The shading models are not registry nodes.** standard_surface, open_pbr_surface,
  gltf_pbr, disney_principled and UsdPreviewSurface are dispatched only by
  `EvalGraph::_EvalMaterialModel()` (`MXCPP/graph.cpp:1000-1150`, names at `:108-143`) and
  must be the surface terminal itself (`MXCPP/graph.cpp:531-548`). (`ND_surface`,
  `ND_surface_unlit`, `ND_volume` and `ND_mix_surfaceshader` are both dispatched as terminals
  and registered, `MXCPP/nodeRegistry.cpp:137-141`, so they may also sit upstream.) So
  **`flattenSubgraphs()` must not run unfiltered**: it inlines every node with a node-graph
  implementation (`MaterialXCore/Node.cpp:251-404`), which turns `standard_surface`
  (`libraries/bxdf/standard_surface.mtlx:107`) into its pbrlib BSDF network and loses the
  native model. With the filter of step 3 it inlines only what MaterialXCpp lacks.
  `flattenSubgraphs()` doesn't descend into separate node graphs, so it runs on each
  (`MaterialXView/Viewer.cpp:1375-1386` does the same).
- **Defaults need no work**: `EvalGraph::Compile()` connects unconnected `texcoord`, position,
  normal, tangent and bitangent inputs itself (`MXCPP/graph.cpp:244-399`), the equivalent of
  MaterialX's `defaultgeomprop`. `ND_texcoord_*` ignore `index` and return the context's
  texture coordinates (`MXCPP/nodes/geometricNodes.cpp:167-178`), which halcyon's pathtracer
  takes from `attribute0` (as VisRTX's MDL texture coordinate 0).
- **What still applies from `mxcppAdapter.cpp`**: the `ND_UsdPrimvarReader_*` /
  `ND_UsdTransform2d` renames and their input renames (`varname`→`geomprop`,
  `fallback`→`default`; `in`→`texcoord`, `rotation`→`rotate`, `translation`→`offset`), the
  value conversion, and the colour handling, with MaterialX colour-space names mapped (Q4).
  Not: the native `Usd*` ids (`UsdPrimvarReader_float`, `UsdTransform2d`), which a `.mtlx`
  can't contain, and `SdfAssetPath` resolution.
- **Prototype** (throwaway, not committed; built against the local MaterialX build in
  `~/build/MaterialX`, installed to `~/opt/.install/MaterialX`, from `~/src/MaterialX` at
  `38368ee0`): a 150-line converter of steps 1-4 against
  MaterialX 1.39.6 and halcyon's vendored MaterialXCpp loaded all 54 materials of MaterialX's
  `resources/Materials/Examples` (standard_surface, open_pbr, gltf_pbr, UsdPreviewSurface,
  disney_principled, including the procedural marble and brick, the tiled brass and wood and
  the chess set). 53 compile; `simple_hair_default` fails on MaterialX's hair helpers
  (`ND_chiang_hair_absorption_from_color`, `ND_chiang_hair_roughness`,
  `ND_deon_hair_absorption_from_melanin`), which MaterialXCpp doesn't have. Shader nodes stay
  single native nodes (`Tiled_Brass` is 3 nodes: two `ND_tiledimage_*` and the
  standard_surface). The same converter with an unfiltered flatten produced 23–105-node BSDF
  networks of `ND_surface` instead.
- **Limits that follow from MaterialXCpp**: a shading model can't sit upstream of another
  node (a `mix_surfaceshader` of two standard_surfaces is Invalid: the models aren't in the
  registry, `MXCPP/graph.cpp:693-698`); `ND_mix_surfaceshader` works over `ND_surface`-level
  closures only. Lama nodes aren't implemented.

### 3. Displacement and volume terminals

- **Volume**: a `volume` terminal (an `ND_volume` or `ND_mix_volumeshader` over VDF nodes) is
  wrapped with the surface into `ND_hdembree_surface_volume_material`, a transparent boundary
  with an interior medium (`MXCPP/graph.cpp:412-444`, `:510-512`). Halcyon passes it through;
  the medium reaches the integrator through `PathMaterial_mediumSoA()` (ADR 0004), whose
  transport ticket 17 implemented (AGENTS.md, "Interior media").
- **Displacement**: only `ND_displacement_float` (`MXCPP/graph.cpp:533-538`), evaluated by
  `EvaluateDisplacement()` (`MXCPP/graph.h:98-105`), which Typhoon applies only to refined
  subdivision surfaces (`TY/renderer/geometry/displacementEvaluation.cpp:427`, `:514`;
  `TY/ARCHITECTURE.md:474-482`). Halcyon has no subdivision, so 22 compiles the terminal to
  check it, reports it unused at DEBUG level; ticket 27 put subdivision and displacement
  out of scope, so the terminal stays compiled and unused.
- None of Typhoon's test scenes uses either terminal.

### 4. File textures, relative paths and colour spaces

- **Paths**: Typhoon's OIIO backend takes `filePath` as it is (`TY/renderer/materials/
  oiioTextureSystem.cpp:120-140`); UsdImaging resolved it. Halcyon resolves it in the
  converter: `getResolvedValueString()` applies `fileprefix` and token substitutions for
  `filename` inputs (`MaterialXCore/Element.cpp:598-626`, `:668-679`, `:934-945`), then a
  relative result is joined to the document's directory. Example documents rely on both
  (`standard_surface_brass_tiled.mtlx:2`: `fileprefix="../../../Images/"`).
- **Colour-space names**: MaterialX names colour spaces `srgb_texture`, `lin_rec709`,
  `acescg`, ... (`MXCPP/nodes/colorTransformNodes.cpp:100-216`); Typhoon resolves names only
  through `GfColorSpace`, without aliases (`TY/renderer/colorManagement.cpp:77-100`), and so
  does halcyon's port (`renderer/pathtracer/ColorManagement.h:29-34`). OpenUSD's nanocolor
  table knows only `*_scene` and descriptive names (OpenUSD at the pin,
  `pxr/base/gf/nc/nanocolor.c:891-908`).
  Typhoon's own scenes use those names (`srgb_rec709_scene` ×11, `lin_rec709_scene` ×5, ...).
  The converter therefore maps MaterialX names to OpenUSD's: `lin_rec709`→`lin_rec709_scene`,
  `srgb_texture`→`srgb_rec709_scene`, `acescg`→`lin_ap1_scene`, `g22_rec709`→
  `g22_rec709_scene`, `g18_rec709`→`g18_rec709_scene`, `g22_ap1`→`g22_ap1_scene`,
  `lin_adobergb`→`lin_adobergb_scene`, `adobergb`→`g22_adobergb_scene`, `lin_displayp3`→
  `lin_p3d65_scene`, `srgb_displayp3`→`srgb_p3d65_scene`, `none`/`raw`→ no transform;
  `rec709_display` has no OpenUSD equivalent and warns.
- **Which inputs**: an input's *active* colour space (`getActiveColorSpace()`, which inherits
  from node, graph and document, `MaterialXCore/Element.h:258-268`) applies to color3/color4
  constants (converted into the working space, as `_ConvertAuthoredColorParameters()` does)
  and to `filename` inputs of image nodes with colour outputs (a `colorSpace:file`
  parameter, which image nodes put into the request, `MXCPP/nodes/textureNodes.cpp:30`,
  `:426-428`; the backend converts sampled colour data, ADR 0005).
- **Through usdMtlx, Typhoon differs**: usdMtlx writes `colorSpace` only where it differs
  from the document's (OpenUSD at the pin, `pxr/usd/usdMtlx/reader.cpp:457-467`) and keeps
  MaterialX's names, which Typhoon's resolver doesn't know, so it warns and leaves
  `srgb_texture` images unconverted. This design converts them, following MaterialX (decision
  C). Image comparisons with Typhoon of documents that name colour spaces other than the
  working space therefore need copies with OpenUSD's names on Typhoon's side.
- **The `colorTransform` callback** (`MXCPP/shadingContext.h:101-114`) isn't involved:
  `colortransform` nodes call it with MaterialX names, Typhoon installs none, and halcyon's
  (`ColorManagement.h:39-51`) declines them, so both use MaterialXCpp's built-in conversions.
- **Formats**: MaterialX's example documents use JPEG (72 references) and PNG (5). Typhoon's
  test scenes use PNG, ASCII PPM (P3, three 8×8 colour-management patterns) and Radiance HDR;
  the one EXR is a commented-out absolute path. The `.tx` files in
  `testMaterialXCppNodes.cpp` (`/tmp/test_albedo.tx`, `/tmp/test_env.tx`,
  `/tmp/triplanar_x.tx`, ...) are keys for a mock texture system (`_TestTextureSystem`,
  `:128-145`), not files. So the concern of ticket 12's follow-up (the loader question),
  that 22's acceptance fails on `.tx`, doesn't hold for Typhoon's data; it holds for
  production assets (EXR, `.tx`, TIFF, UDIMs).

### 5. Dependency (decision A1, below)

MaterialXCore has no dependencies; MaterialXFormat compiles in pugixml 1.9 in the global
`pugi` namespace. Statically linked, the two add about 1.2 MB of code (≈916 KB + 273 KB text);
loading the data libraries takes about 20 ms, importing them into a document about 10 ms
(measured here, 56 files, 817 nodedefs). The nodedef files a converter needs (stdlib,
pbrlib, bxdf, nprlib, cmlib definitions and node graphs, without `gen*`, `mdl`, `targets`)
are 31 files, 960 KB.

Three hazards apply whatever is chosen:

- **Symbols in a USD process**: OpenUSD 26.05 here links the same MaterialX 1.39.6 statically
  into `libusd_usdMtlx.so` and exports `MaterialX_v1_39_6::` symbols. Halcyon loaded into
  usdview (through hdAnari) would hold a second copy under the same names; pugixml's would
  clash with any other static pugixml. Linking MaterialX statically with
  `--exclude-libs`, as for Embree, keeps the device's copy private.
- **CMake targets**: `pxrConfig.cmake` runs `find_dependency(MaterialX)`
  (`OpenUSD-26.05/pxrConfig.cmake:80-91`), so a configure that finds OpenUSD (as the
  Typhoon reference harness's did, since removed) already defines `MaterialX*` targets; a fetched MaterialX must not define the same
  targets (VisRTX hit this, `rtx/CMakeLists.txt:203-208`).
- **Finding the data libraries**: `getDefaultDataSearchPath()` searches from the executable,
  not the device library (`MaterialXFormat/Util.cpp:229-262`, `File.cpp:332`, `:370`), so a
  device can't rely on it; VisRTX needs a four-step search chain and a device parameter.
  Embedding the 31 definition files in the device (generated at build time from the
  MaterialX the device links) removes the runtime lookup and the version skew between the
  code and the definitions.

## Extension name (ticket 22 left it to this ticket)

This note first proposed declaring VisRTX's `VISRTX_MATERIAL_MATERIALX` with its JSON copied
into halcyon, so that apps that check VisRTX's name work with halcyon unchanged. The
maintainer chose otherwise (decision D2, below): halcyon declares its own
`HALCYON_MATERIAL_MATERIALX` (`extensions/halcyon_material_materialx.json`, read as an extra
JSON root like `HALCYON_MATERIAL_OPENPBR`) with VisRTX's parameter names and properties, so
a halcyon device doesn't advertise another vendor's extension, its additions (samplers of
every subtype on any input through the sampler node, attribute names, a volume terminal,
colour-space conversion) aren't behaviour under VisRTX's name, and VisRTX's changes to its
extension don't bind halcyon.

## Decisions (the maintainer's, 2026-10-04)

- **A1**: MaterialX fetched at configure time (pinned 1.39.6, only Core and Format), linked
  statically with hidden symbols, the definitions embedded; an installed MaterialX 1.39
  with static libraries used instead when found. (As implemented: an installed MaterialX
  1.39.6 or later, with its example resources; otherwise commit `38368ee0` (1.39.6) built
  under halcyon's own target names, `halcyon_materialx_core`/`_format`, and namespace,
  `MaterialX_halcyon_v1_39_6`, so that OpenUSD's MaterialX targets in the same configure
  don't clash.)
- **B2**: stb_image plus tinyexr plus a small ASCII PNM reader; OpenImageIO only if a ticket
  needs `.tx`, TIFF or UDIM.
- **C1**: MaterialX's colour-space semantics, a deliberate difference from Typhoon reading
  the same documents through usdMtlx (for ticket 28's list).
- **C2**: samplers of any subtype on any input.
- **C3**: ticket 22's acceptance replaced as proposed under Consequences.
- **D2**: `HALCYON_MATERIAL_MATERIALX`, with VisRTX's parameter names.

Confirmed later the same day, from the implementation (pathtracer ticket 22):

- Attribute names (a STRING naming an attribute) as overrides on shader inputs, beyond
  VisRTX's contract.
- The `preview` renderer's view of a `materialx` material as its surface shader's diffuse
  colour and opacity, as for openPBR and usdPreviewSurface (Consequences), although a
  document whose colour comes from a texture then looks flat there.
- The repeated flush of the commit buffer for renders and `ANARI_WAIT` queries
  (Consequences, up to 8 passes); a helium fix, a flush that drains the observers staged
  during it, is tracked outside halcyon.

The options as they were put to the maintainer:

### A. How halcyon gets the MaterialX library (Q5)

| Option | What | For | Against |
|---|---|---|---|
| **A1** | Fetched at configure time (FetchContent, pinned 1.39.x, only Core and Format, generators and tests off), static, `--exclude-libs`; an installed MaterialX of the right version used instead when found, as for Imath | Always available, like Embree, OpenQMC and Imath; consistent with "always build the pathtracer" (ticket 31); one version for code and embedded definitions | Configure builds ~21 more source files; target-name clash with USD's MaterialX in reference-harness configures must be avoided (prefer the found package there) |
| **A2** | Optional `HALCYON_ENABLE_MATERIALX` (default ON when `find_package(MaterialX 1.39)` succeeds), like VisRTX | No new fetched dependency; builds without MaterialX still work | Brings back a conditional device definition (the filtered copy ticket 31 removed); the subtype silently disappears where MaterialX isn't installed; distro packages are rare (none here) |
| **A3** | Required `find_package(MaterialX)` | Simplest CMake | Every build needs an installed MaterialX |
| **A4** | Vendor MaterialXCore/Format sources under `external/` (like Typhoon's) | No network, no package | ~16k lines of MaterialX plus ~14k of pugixml in the repo, with a pin to move by hand |
| **A5** | No MaterialX library: a hand-written reader (pugixml) with nodedef resolution | Smallest dependency | Reimplements nodedef matching, versions, inheritance, flattening and string resolution; error-prone |

**Recommendation: A1**, with the definitions embedded, and an installed MaterialX preferred
when present (the reference-harness configure finds USD's). It keeps `materialx` in every
build, as the pathtracer is, and the `.mtlx` definitions always match the code.

### B. Image file loader (bears on ticket 12's follow-up)

| Option | Formats | For | Against |
|---|---|---|---|
| **B1** | stb_image (ADR 0005's design) | PNG, JPEG, HDR, TGA, BMP, binary PNM: every image in MaterialX's examples and Typhoon's test data except the three ASCII (P3) PPMs; header-only; what VisRTX and the ANARI SDK use | No EXR, TIFF or `.tx`; mips are halcyon's box filter, not OIIO's |
| **B2** | stb_image + tinyexr (+ a few lines for ASCII PPM) | Adds EXR, the common production HDR format; still two headers (tinyexr brings miniz) | No `.tx`/TIFF/UDIM; filtering still differs from OIIO |
| **B3** | OIIO optional (`HALCYON_ENABLE_OIIO`, used when found; Ubuntu ships 2.4 here) behind the same keys, B1/B2 otherwise | Typhoon's backend: `.tx`, TIFF, EXR, UDIM, the same mip filtering, closest image parity | Large dependency tree; two code paths whose images differ; OIIO's own symbols in a USD process |
| **B4** | Defer until assets need more than B1 | Nothing to build now | Production `.mtlx` with EXR or `.tx` render textures as missing |

**Recommendation: B2 now** (it covers MaterialX's and Typhoon's data and the most common
production format), **B3 later** if a ticket needs `.tx`/UDIM or exact filtering parity.
Typhoon parity of texture filtering is then a known difference at minification only.

### C. Departures this design makes (confirm or reject)

AGENTS.md's policy asks for the maintainer's decision on departures from the reference:

- **Colour spaces**: halcyon understands MaterialX's colour-space names and honours the
  document's colour space, where Typhoon reading the same `.mtlx` through usdMtlx leaves
  `srgb_texture` images unconverted with a warning (Q4). Recommended: follow MaterialX and
  record it as a deliberate difference for ticket 28; the alternative is to match Typhoon's
  usdMtlx path (no alias table, document colour space ignored).
- **Samplers beyond VisRTX's rule**: on non-`filename` inputs, through the sampler node, and
  of every sampler subtype (VisRTX: image2D only, spliced image node, `attribute0`).
  Recommended: allow them (the sampler node already does it exactly for physicallyBased);
  the alternative is VisRTX's rule and warning.
- **22's acceptance** (Consequences): Typhoon's test data holds no `.mtlx`, so 22 needs the
  replacement acceptance proposed below.

## Consequences

- 22's acceptance changes: Typhoon's `MaterialXCpp/tests/data` are USD scenes with inline
  UsdShade networks (57 `.usda`, no `.mtlx`), and hdAnari has no MaterialX path
  (`hdanari/rd/material.cpp:186-200` maps any non-UsdPreviewSurface network to `matte`), so
  they can't be "loaded as documents". Proposed instead: MaterialX's example documents load
  and compile (53 of 54), and a chosen set renders like Typhoon (01) rendering the same
  `.mtlx` through usdMtlx (a USD stage that references the document), with OpenUSD
  colour-space names on Typhoon's side where they differ (Q4). (Counted again by
  `materialx_document`: the 50 example documents hold 66 materials, counting the chess
  set's and the look documents' each, and all but the hair's compile and render. Typhoon
  comparison of the example documents was accepted on the maintainer's own testing, not
  a script in the repo; the ticket 28 sweep compares `materialx` materials translated
  from Typhoon's UsdShade test scenes.)
- If decision C accepts it, halcyon departs from Typhoon-via-usdMtlx in colour spaces
  (MaterialX names understood, document colour space honoured): a deliberate difference
  for ticket 28.
- New node types: none. The converter only uses MaterialXCpp's and halcyon's sampler node,
  which is already in the context-group table (`renderer/pathtracer/ContextGroups.cpp`).
- The preview renderer shows a `materialx` material as its surface shader's diffuse colour
  and opacity (the model's input of that name: an override, else the document's constant,
  else the nodedef's default), as it shows openPBR and usdPreviewSurface; other shaders
  white and opaque. (This note first said white and opaque for all.)
- A material's validity now changes with its parameters, so surfaces observe `materialx`
  materials (`Material::validityCanChange()`), and renders and `ANARI_WAIT` queries flush
  the commit buffer again while finalizations have staged observers
  (`HalcyonDeviceGlobalState::flushCommitBuffer()`), so a render sees them.

## Considered options

- **Generate code like VisRTX (MaterialXGenMdl/GenOsl)**: halcyon has no MDL or OSL runtime,
  and the pathtracer evaluates graphs with MaterialXCpp (ADR 0003, ADR 0004).
- **Go through USD (usdMtlx → UsdShade → HdMaterialNetwork2 → `mxcppAdapter`)**: Typhoon's
  path exactly, but it puts OpenUSD in the shipped device, which ticket 30 removed.
- **Flatten everything to BSDF level**: MaterialXCpp would evaluate `ND_surface` networks
  instead of its native models, so images would no longer be Typhoon's.
- **Re-read the document for every override (VisRTX's splice and re-transcode)**: the graph
  is a plain map, so overrides edit it directly; only `source`, `sourceType` or
  `materialName` changes re-read.

## Task breakdown for ticket 22

Done in ticket 22 (tasks 1-6 and 8; the Typhoon comparison of task 6 accepted on the
maintainer's own testing; task 7 wasn't needed). The breakdown as planned:

1. **Dependency** (after decision A): bring in MaterialXCore/Format per A1, static and
   hidden (`--exclude-libs`, extend `device_dependencies` to check no `MaterialX` or `pugi`
   symbols are exported); embed the 31 definition files; THIRD-PARTY.md (Apache-2.0, pugixml
   MIT). Test: a ctest that loads the embedded library (817 nodedefs or so) without any
   search path or environment variable.
2. **Converter** (`renderer/pathtracer/MaterialXDocument.{h,cpp}`, helium-free like
   `MaterialConverter`): read, select, filtered flatten, convert, terminals, colour spaces,
   file paths; returns the graph (`ConvertedMaterial`), the material names, the texture
   inputs and diagnostics. Its input is plain data: `sourceType`, `source`, `materialName`
   and the overrides, each a name with a value (`mxcpp::Value`), a path, or a sampler's ISPC
   object (the `const void *` `MaterialParameter` already uses), which the converter appends
   to `ConvertedMaterial::samplers` and names by index.
   Tests (TDD, no device): MaterialX's examples load and compile (hair excluded and
   reported); node counts stay native; `fileprefix` and relative paths; `nodegraph`/`output`
   and `interfacename` connections; colour-space mapping and document inheritance;
   `ND_UsdPrimvarReader_*`/`ND_UsdTransform2d` renames; parse errors and missing
   `materialName` give diagnostics, not exceptions. If 22 registers a node type of its own
   after all, add it to `NODE_READS` (`renderer/pathtracer/ContextGroups.cpp`), with ticket
   33's optional test that every node type halcyon registers is listed there.
3. **Overrides** in the converter: values by leaf name, strings and samplers on `filename`
   inputs (`anari:sampler/<index>`), samplers on other inputs (sampler node); unknown names
   reported. Tests on the graph.
4. **Subtype** `materialx` in `scene/surface/material/`: parameters (`sourceType`, `source`,
   `materialName`, any other name as an override), properties `materialNames` and
   `textureInputs` (`ANARI_STRING_LIST`, after a successful commit), sampler observation as
   in ADR 0005, re-read only when the source, its file's modification time or the selection
   changes; ERROR and `invalidMaterialColor` on failure; the extension in
   `HalcyonDefinitions.json`. Device tests: inline and file documents, both properties,
   overrides, errors, `anariInfo` lists the subtype.
5. **File textures** (after decision B): ADR 0005's file-backed design in
   `HalcyonTextureSystem` (device cache, mips, `t` flip, colour conversion via
   `colorSpace:file`, warn once), with the chosen loader. Tests: PNG/JPEG/HDR (and EXR) load,
   sRGB conversion, flip, address modes, a missing file.
6. **Renders**: MaterialX example documents through the pathtracer (no errors, no
   invalid-material fallbacks), and against Typhoon (01) rendering the same documents through
   usdMtlx, within noise; document the deliberate colour-space difference for 28.
7. **Optional (VisRTX parity)**: VisRTX's nodedef auto-instantiation when a document has no
   material (`Transcoder.cpp:240-300`), only if an app needs it.
8. **Docs**: AGENTS.md (subtype, dependency, colour spaces, limits: hair helpers, models
   upstream of other nodes, displacement unused), this ADR to `accepted`.
