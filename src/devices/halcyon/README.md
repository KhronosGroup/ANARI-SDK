# The Halcyon device

Halcyon is an [ANARI](https://www.khronos.org/anari) device that renders with
[Embree](https://www.embree.org) ray tracing and [ISPC](https://ispc.github.io) shading. It is
built on helium, the device framework in the ANARI SDK, and has two renderers:

- `preview` aims to match the features and images of helide, the SDK's Embree reference
  device: a simple eye-lit renderer with debug visualization modes, surfaces and volumes,
  one sample per pixel, plus screen-space ambient occlusion. It is listed first among the
  renderer subtypes, so `default` creates it too.
- `quality` is a port of [Typhoon](https://github.com/NVIDIA-Omniverse/OpenUSD/tree/typhoon/main),
  NVIDIA's path tracer for OpenUSD (the hdEmbree delegate of the `typhoon/main` branch),
  with its MaterialX materials: a progressive, physically based renderer lit by ANARI's
  lights (see [The quality renderer](#the-quality-renderer)). The name is VisRTX's for its
  path tracer; the docs also call it the pathtracer.

## Dependencies

- [ANARI-SDK](https://github.com/KhronosGroup/ANARI-SDK) 0.15 or newer (`find_package(anari)`),
  which provides helium and the code generation used for introspection
- the ISPC compiler, found by CMake on `PATH` or given with `-DCMAKE_ISPC_COMPILER=...`
- CMake 3.19 or newer (for its ISPC language support; `ctest --test-dir` below needs 3.20) and
  a C++17 compiler

Embree is not a dependency to install: like helide, halcyon's build fetches Embree 4.3.3 with
the ANARI SDK's `anari_sdk_fetch_project()` (see `external/embree/CMakeLists.txt`) and links it
statically, with Embree's internal tasking system. The first configure downloads the source
into `.anari_deps/` in the source tree, and later configures reuse it. Embree is built for
SSE2, SSE4.2, AVX, AVX2 and AVX-512 on x86 (AVX-512 off on Windows and macOS) and picks the
best one the CPU supports at runtime; on ARM it is built for NEON2X. The `EMBREE_ISA_*` cache
options select other ISAs, for example `-DEMBREE_ISA_AVX=OFF` for a shorter build.

The `quality` renderer reuses Typhoon's material library (MaterialXCpp and BSDL),
vendored unmodified in `external/typhoon/` (see its `PROVENANCE.md`). That needs
[Imath](https://github.com/AcademySoftwareFoundation/Imath) 3: the system's if CMake finds
it, otherwise the build fetches Imath 3.1.12 the same way as Embree. It also fetches the
header-only [OpenQMC](https://github.com/AcademySoftwareFoundation/openqmc) 0.7.1, whose
blue-noise tables the pathtracer's sample generator compiles in. For the `materialx`
material subtype (MaterialX documents, `HALCYON_MATERIAL_MATERIALX`) it uses
[MaterialX](https://github.com/AcademySoftwareFoundation/MaterialX)'s Core and Format
libraries: an installed MaterialX 1.39 (1.39.6 or later) with static libraries if CMake
finds one, otherwise the build fetches MaterialX 1.39.6 (about 115 MB, most of it
MaterialX's example resources, which the tests use) the same way, and embeds its data
library in the device. It also fetches [stb_image](https://github.com/nothings/stb) and
[tinyexr](https://github.com/syoyo/tinyexr), which read the image files documents name.
Halcyon is licensed under Apache-2.0, as the rest of the ANARI SDK (`LICENSE` at the SDK's
root); third-party licenses, and the ported
files that carry them (Typhoon's TOST 1.0, `LicenseRef-TOST-1.0`; Apache-2.0), are listed
in `THIRD-PARTY.md`.

Neither the device nor its tests need OpenUSD.

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/anari
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This builds the device library `libanari_library_halcyon.so` into the build directory. The unit
tests (ctests named `halcyon::<name>`) are on by default; turn them off with
`-DHALCYON_BUILD_TESTS=OFF`.

In the ANARI SDK's tree halcyon is `src/devices/halcyon`, off by default: configure the SDK
with `-DBUILD_HALCYON_DEVICE=ON`.
`cmake --install build --prefix <prefix>` installs only the library (into `<prefix>/lib`, or
your platform's library directory).

## Using halcyon

Applications load halcyon like any other ANARI device, by library name:

```c
ANARILibrary lib = anariLoadLibrary("halcyon", statusCallback, NULL);
ANARIDevice dev = anariNewDevice(lib, "default");
```

The directory holding `libanari_library_halcyon.so` has to be on the dynamic loader's search
path (for example `LD_LIBRARY_PATH`). To check which extensions and subtypes the device reports:

```sh
LD_LIBRARY_PATH=build anariInfo -l halcyon
```

`anariInfo` lists the device's extensions, those of all its renderers; each renderer's own
list comes from `anariGetObjectInfo(device, ANARI_RENDERER, subtype, "extension",
ANARI_STRING_LIST)` (see [Supported extensions](#supported-extensions)).

Applications can also link the library directly and create the device with
`anariNewHalcyonDevice(statusCallback, userData)`, declared in
`include/anari/ext/halcyon/anariNewHalcyonDevice.h`. That header is **not installed**, and there
is no CMake package for halcyon. It is only usable from a build tree: add `include/` from the
source tree and the build directory (for the generated `anari_library_halcyon_export.h`) to the
include path, and link `libanari_library_halcyon`.

The `HALCYON_NUM_THREADS` environment variable sets the number of threads Embree's tasking
system uses for both BVH builds and rendering (default: all hardware threads).

## Supported extensions

As declared in `HalcyonDefinitions.json`. The device reports all of them; each renderer
reports only those it honours (renderer introspection and the renderer's `extension`
property): the `preview` renderer all but accumulation and the lights other than the
hdri (which it shows where rays miss, without being lit by it), the `quality` all of
them.

- Device and frames: `ANARI_KHR_DEVICE_SYNCHRONIZATION`, `ANARI_KHR_FRAME_COMPLETION_CALLBACK`,
  `ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID`, `ANARI_KHR_FRAME_CHANNEL_OBJECT_ID`,
  `ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID`, `ANARI_KHR_FRAME_CHANNEL_ALBEDO`,
  `ANARI_KHR_FRAME_CHANNEL_NORMAL`, `ANARI_KHR_FRAME_ACCUMULATION` (`quality` only)
- Cameras: `ANARI_KHR_CAMERA_PERSPECTIVE`, `ANARI_KHR_CAMERA_ORTHOGRAPHIC`
- Instances: `ANARI_KHR_INSTANCE_TRANSFORM`, `ANARI_KHR_INSTANCE_TRANSFORM_ARRAY`
- Geometry: `ANARI_KHR_GEOMETRY_TRIANGLE`, `ANARI_KHR_GEOMETRY_QUAD`,
  `ANARI_KHR_GEOMETRY_SPHERE`, `ANARI_KHR_GEOMETRY_CURVE`, `ANARI_KHR_GEOMETRY_CYLINDER`,
  `ANARI_KHR_GEOMETRY_CONE`, and halcyon's `ANARI_HALCYON_GEOMETRY_CURVE_BASIS` (the curve's
  `basis`: `linear`, `bspline` or `catmullRom`; cubic segment i spans control points i to
  i + 3, and apps repeat the end points to reach them)
- Materials: `ANARI_KHR_MATERIAL_MATTE`, `ANARI_KHR_MATERIAL_PHYSICALLY_BASED`, and
  halcyon's `ANARI_HALCYON_MATERIAL_OPENPBR` (subtype `openPBR`) and
  `ANARI_HALCYON_MATERIAL_USD_PREVIEW_SURFACE` (subtype `usdPreviewSurface`), whose
  parameters are the MaterialX inputs of the same names, and
  `ANARI_HALCYON_MATERIAL_MATERIALX` (subtype `materialx`: a material of a MaterialX
  document, with VisRTX's `VISRTX_MATERIAL_MATERIALX` parameters) (`extensions/*.json`)
- Samplers: `ANARI_KHR_SAMPLER_IMAGE1D`, `ANARI_KHR_SAMPLER_IMAGE2D`,
  `ANARI_KHR_SAMPLER_IMAGE3D`, `ANARI_KHR_SAMPLER_PRIMITIVE`, `ANARI_KHR_SAMPLER_TRANSFORM`
- Volumes: `ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR`,
  `ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D` (the `quality` scatters light in them, with
  their colour as the albedo; the `preview` renderer composites it as emission)
- Lights (`quality` only): `ANARI_KHR_LIGHT_DIRECTIONAL`, `ANARI_KHR_LIGHT_POINT`,
  `ANARI_KHR_LIGHT_SPOT`, `ANARI_KHR_LIGHT_QUAD`, `ANARI_KHR_LIGHT_RING`,
  `ANARI_KHR_LIGHT_HDRI`, `ANARI_KHR_LIGHT_PRIMARY_VISIBILITY`; the `preview` renderer
  also lists `ANARI_KHR_LIGHT_HDRI`: it shows the `visible` hdris in place of the
  background, as the `quality` does, but isn't lit by them
- Renderer: `ANARI_KHR_RENDERER_AMBIENT_LIGHT`, `ANARI_KHR_RENDERER_BACKGROUND_COLOR`,
  `ANARI_KHR_RENDERER_BACKGROUND_IMAGE`

The `preview` renderer's `mode` parameter selects debug visualizations (ids, normals,
attributes, opacity heatmap and more). Its values are listed in `HalcyonDefinitions.json`
and available through parameter introspection.

Triangle and quad geometry read shading normals (`vertex.normal`, `faceVarying.normal`), which
the eye light, ambient occlusion, the `worldNormal`/`objectNormal` attributes and the `Ns`
modes use; helide ignores them. Geometry without them shades with its geometric normal.

Parameter defaults are the ones introspection reports, some of which differ from helide's:
the renderer's `ambientRadiance` 0 (helide 1; used only with `ignoreAmbientLighting` false)
and matte `color` 0.8 grey (helide white). helide 0.16 and older also default sphere
`radius` to 0.01 and `volumeSamplingRate` to 1.0 (halcyon, and helide 0.17: 1.0 and 0.5).

Ambient occlusion (halcyon only; helide has none) darkens surfaces where nearby geometry
occludes them. It is computed in screen space, one sample per pixel, so it stays
interactive. The `preview` renderer's parameters:

- `ambientOcclusion` (bool, default true): darken the first visible surface of each pixel
  in the `default` mode. It is on by default, so set it to false to match helide's images.
- `ambientOcclusionRadius` (float, default 0): the world-space distance within which
  geometry occludes. 0, or a negative value, uses 3 percent of the diagonal of the world's
  bounds.
- `ambientOcclusionEstimator` (string, default `"g3d"`): `"g3d"` uses G3D's high-quality
  Scalable Ambient Obscurance estimator, `"sao"` the form of the SAO paper (McGuire et al.,
  HPG 2012). Neither depends on the scene's units: scaling a scene and the radius together
  gives the same image.
- `ambientOcclusionIntensity` (float, default 1): an exponent applied to the occlusion
  factor. Above 1 darkens, below 1 lightens, and 0 turns the darkening off.
- `ambientOcclusionBlurPasses` (int, default 1, 1 to 5): how many times the occlusion is
  blurred. One pass removes its noise; faces seen from a low camera just above a floor can
  still show faint horizontal bands, which 3 passes remove at the cost of the extra passes.
  Values outside 1 to 5 are clamped.

Only the first visible surface is darkened, also where it is seen through a volume or a
`mask` cut-out. Surfaces behind a `blend` surface, volumes and the background are not. The
`ambientOcclusion` and `ambientOcclusion.unblurred` modes show the occlusion factor as gray
(white is unoccluded), after and before the blur that removes its noise, whether or not
`ambientOcclusion` is set.

Device parameters: `allowInvalidMaterials` (bool, default true) renders surfaces without a
valid material in `invalidMaterialColor` (float4, default magenta) instead of leaving them out.
Committing the device makes the next render of every frame render, even if nothing else changed.

## The quality renderer

Select it with `anariNewRenderer(device, "quality")`. Its reference is Typhoon, not
helide; `docs/typhoon-verification.md` records how the two compared on Typhoon's test
scenes, and
`AGENTS.md` lists where halcyon deliberately differs.

- **Progressive:** every render adds `pixelSamples` samples to each pixel that hasn't
  converged, whatever the frame's `accumulation` says, until every pixel passes Typhoon's
  adaptive sampling test or the sample limit (`convergedSamplesPerPixel`, or VisRTX's
  `sampleLimit`; 256 by default) is reached. The frame's `refinementProgress` (and
  `numSamples`) say how far it got; any change starts over.
- **Integrator:** Typhoon's unidirectional path tracer: next-event estimation from every
  light weighed against BSDF sampling by multiple importance sampling, `maxBounces` (16),
  Russian roulette, firefly clamping, transparent shadows, its blue-noise Sobol sampler
  (`randomNumberSeed`), and with `enableCaustics` (off by default) caustic paths.
- **Materials:** every material is a MaterialX graph shaded by Typhoon's closures: `matte`,
  `physicallyBased` (glTF PBR), `openPBR`, `usdPreviewSurface` and `materialx` documents.
  `matte` and most `physicallyBased` materials are also shaded natively in ISPC, several
  times faster and matching their graphs (`nativeMaterials`, true by default). Samplers of
  every subtype and image files named by documents texture them.
- **Lights:** `directional` (with an `angularDiameter`, a sun disc), `point` (with a
  `radius`, a sphere light), `spot`, `quad`, `ring` (both with `intensityDistribution`)
  and `hdri` (a dome), in ANARI's radiometric units, plus the renderer's ambient light
  (`ambientColor` × `ambientRadiance`, a constant dome). `visible` lights are seen by
  camera rays; the `background` shows only where camera rays miss and lights nothing.
- **Transport:** subsurface scattering (Typhoon's random walk), homogeneous interior media
  (transmission depth and scatter, MaterialX volume shaders), hero-wavelength dispersion,
  and scattering in `transferFunction1D` volumes, whose colour is the albedo.
- **Channels:** colour, depth and ids as in the `preview` renderer; albedo and normal from
  each path's first non-specular hit, for denoisers.
- **Debug modes:** `mode` shows one value of the shading context (`position`, `normal`,
  `tangent`, `texcoord`, derivatives, ...) instead of path tracing.

Gaps: no subdivision surfaces or displacement (a material's displacement is ignored), motion
blur or denoiser (Typhoon has none either); next-event estimation samples every light, so
scenes with many lights are slow; area lights have no textures, IES profiles or colour
temperature; MaterialX's hair nodes don't compile; image files have no `.tx`, TIFF or UDIM
support and ANARI samplers no mip levels; meshes without normals shade faceted, where
Typhoon smooths them; a path is in one interior medium at a time. Curves are linear,
B-spline or Catmull-Rom (`HALCYON_GEOMETRY_CURVE_BASIS`), not Bézier or ribbons.

## Known limitations

- The `preview` renderer ignores lights (only the `quality` reads them; without it, light
  objects can be created but do nothing). Its shading is an eye light blended with a constant
  ambient term, and `ambientColor` is ignored (both as in helide).
- The `preview` renderer takes one sample per pixel and doesn't accumulate. Primary rays go
  through pixel centres, as helide 0.17's do; helide 0.16 and older go through pixel
  corners, so their images are shifted by half a pixel from halcyon's.
- In the `preview` renderer, material opacity and `alphaMode` apply only in the `default`
  mode, which composites through `blend` surfaces and skips `mask` cut-outs; other modes
  show the first surface hit as opaque (except `ambientOcclusion` and
  `ambientOcclusion.unblurred`, which also skip surfaces with no opacity). The `preview`
  renderer has no refraction, and a surface coplanar with one in front of it is hidden.
- A thread that has an array mapped and waits on the device (`anariFrameReady` with
  `ANARI_WAIT`, `anariMapFrame`, `anariGetProperty` with `ANARI_WAIT`) gets an error instead of
  a wait. `anariRenderFrame` from such a thread, on a frame whose previous render is still
  queued, is dropped with an error; its completion callback still fires, after the queued
  render.
- Waiting on, mapping or rendering another frame from a completion callback, while that frame's
  render is still queued, also reports an error instead of waiting (a render dropped this way
  still fires its completion callback). Rendering a frame from its own completion callback is
  ignored with a warning and fires no callback.
- Every other `anariRenderFrame` fires the completion callback once, including renders of an
  incomplete frame (which report an error and clear only the color channel) and renders
  skipped because nothing changed.
- Releasing an array while it is still mapped is an error: halcyon unmaps it (keeping what the
  application wrote) and reports an error.
- Releasing the last reference to a shared array (one on application memory, created without
  a deleter) that the scene still uses waits for a render in progress; the device then uses its
  own copy, so the application may free its memory once `anariRelease` returns. A status
  callback running during a render can't wait for it: such a release reports a warning, and the
  memory must stay valid until the render ends.
- A status callback raised during such a release (helium reports the copy) that waits on the
  device gets an error instead of a wait.
- These deadlocks remain: an application thread that has an array mapped and waits on a frame
  whose completion callback calls `anariGetProperty` with `ANARI_WAIT`; a status callback that
  maps an array while a render is running; a status callback raised on one of the render's
  helper threads (none is known to occur) that releases the last reference to a shared array
  the scene uses or waits on the device; and a completion callback that waits on, maps or
  renders another frame while an application thread is waiting on or mapping that frame.
- Most array data is copied when an object is committed, but triangle and quad
  `vertex.position` and a UINT32 `primitive.index` of any geometry but curve are read in
  place from the array's memory.
