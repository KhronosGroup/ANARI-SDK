# The Hydra Device

The hydra device renders ANARI scenes with OpenUSD
[Hydra](https://openusd.org/release/api/_page__hydra__getting__started__guide.html)
render delegates. An ANARI application loads the `hydra` library and picks a
delegate as the device subtype:

```c
ANARILibrary lib = anariLoadLibrary("hydra", statusFunc, NULL);
ANARIDevice dev = anariNewDevice(lib, "storm"); // or "embree", "default", ...
```

The device mirrors the ANARI object graph into a Hydra retained scene index
(zero-copy where the data allows it), and renders it through the delegate on a
worker thread that owns the delegate's GL context. Delegate render settings
appear as renderer parameters, and arbitrary Hydra AOVs as frame channels.

## The hydra device and hdAnari

The ANARI-SDK also ships [hdAnari](../../hdanari), which goes the other way:
hdAnari is a Hydra render delegate that renders USD scenes through an ANARI
device. The hydra device is a forward bridge: ANARI scenes rendered through
Hydra.

The two must not be chained. hdAnari picks its ANARI library through
`ANARI_LIBRARY`, so if that names `hydra`, the device would drive hdAnari, which
would load the device again, and so on without end. The device therefore never
exposes or loads hdAnari: `HdAnariRendererPlugin` is left out of the subtype
list, and the `default` subtype forwards to Storm whenever Storm is installed,
falling back to Hydra's own default delegate choice (never hdAnari) only when it
is not.

## Delegates

Every renderer plugin in the OpenUSD plugin registry becomes a device subtype,
named after its plugin id without the `Hd` prefix and `RendererPlugin` suffix,
lowercased (`HdStormRendererPlugin` becomes `storm`). The device probes the
delegate's capabilities once and reports only the extensions and object
subtypes the delegate can render, so the extension list differs per subtype.

- **Storm** (`storm`, and `default`) is the reference delegate. Everything in
  this README works on Storm unless stated otherwise.
- **HdEmbree** (`embree`) has no material networks, curves, samplers or
  spatial fields. Materials fall back to their constant color and opacity
  (`displayColor`), and the extension list drops what HdEmbree can't do.
- Other delegates (hdGatling, for example) work through the same registry
  but are not covered by the tests.

## Building

Enable the device with `BUILD_HYDRA_DEVICE` (default `OFF`, since it needs
OpenUSD) and point `CMAKE_PREFIX_PATH` at an OpenUSD install that includes
Storm (OpenUSD 25.11 or newer; 26.05 is the primary target):

```
cmake -S ANARI-SDK -B build -DBUILD_HYDRA_DEVICE=ON \
  -DCMAKE_PREFIX_PATH=/path/to/OpenUSD
cmake --build build -j
```

The build also needs Python 3 (for the SDK's query generator) and OpenGL.
libEGL is optional: with it, the device makes its GL context without a display
server (see [GL context](#gl-context)); without it, Storm needs an X display.
Configure prints `EGL: enabled (...)` or `EGL: disabled (...)`.
The device builds `anari_library_hydra` and installs it to the library
directory. The `plugInfo.json` manifest of its in-memory image plugin goes in
an `anari_hydra/` subdirectory beside it, in the build tree and the install
tree alike, so it doesn't collide with other packages' manifests in a shared
prefix. At runtime the OpenUSD libraries must be on the loader path.

NanoVDB headers back the `nanovdb` spatial field and
`KHR_SPATIAL_FIELD_NANOVDB`. By default, configure downloads them from a
pinned OpenVDB release (v13.0.0, MD5-checked) through the SDK's dependency
fetch. Configure prints whether NanoVDB is enabled, and from where:

- `ANARI_HYDRA_NANOVDB_INCLUDE_DIR`: use a local directory containing
  `nanovdb/NanoVDB.h` instead of downloading (offline builds).
- `ANARI_HYDRA_ENABLE_NANOVDB=OFF` (default `ON`): build without NanoVDB, which
  drops the `nanovdb` field subtype and its extension.

## GL context

The device's worker thread makes a GL context current when the device is
created, because Storm's GL backend (HgiGL) binds to the current context.
The environment variable `ANARI_HYDRA_GL` picks how:

- `auto` (default): EGL, else GLX, else none.
- `egl`: an EGL context, which needs no display server. It uses EGL device
  `ANARI_HYDRA_EGL_DEVICE` (default `0`) through `EGL_EXT_platform_device`, or
  Mesa's `EGL_MESA_platform_surfaceless` (e.g. llvmpipe) when there is no
  device platform or the default device doesn't initialize.
- `glx`: a hidden 8x8 GLX window, which needs an X display.
- `none`: no GL context. CPU delegates such as HdEmbree still render.

The device reports its choice as an INFO status message, for example
`hydra: GL context EGL (NVIDIA GeForce RTX 5090/PCIe/SSE2, device 0)`. If the
delegate can't run with what is available, `anariNewDevice` returns `NULL` and
reports `ANARI_STATUS_UNSUPPORTED_DEVICE` through the status callback: Storm
without any GL context, an invalid `ANARI_HYDRA_GL`, or a forced `egl` or `glx`
that is unavailable.

An application with its own GL context, on any thread, is unaffected: the
device's context lives on its worker thread. One exception is handled
automatically: with GLVND, an EGL context can't be made current once the
application has made a GLX context current, so `auto` then falls back to a GLX
context of its own (which needs the X display the application already has).

This directory also builds standalone against an installed ANARI-SDK, as long
as that install includes the code generator (`INSTALL_CODE_GEN_SCRIPTS=ON`);
configure stops with an error otherwise. Standalone, `ANARI_HYDRA_TESTS`
replaces `BUILD_TESTING`.

## Tests

The end-to-end tests in [`tests/`](tests) follow `BUILD_TESTING`. Each one
drives the device only through the public ANARI C API, and most render on
both Storm and HdEmbree. Their names start with `hydra_`, so they are not
picked up by the SDK's `unit_test`/`render_test` filters:

```
cd build && ctest -R '^hydra_'
```

The tests run headless, through EGL. The Storm tests run through
`tests/gl_gate`, which reports them as skipped only on a machine with no GL
context at all (no EGL and no X display); the others always run. The
`*_embree` tests need OpenUSD's optional hdEmbree plugin
and are disabled when it is missing.

## Known limitations

General:

- On Storm, enabling `channel.primitiveId` runs an extra full-viewport picking
  pass, and the object and instance ID channels then come from that pass. Its
  silhouettes can differ from the color and depth channels at edges.
- Introspection lists only the parameters the device honours. Parameters it
  accepts only to warn about and drop (ring `innerRadius`, hdri `up` and
  `layout`, the physicallyBased inputs UsdPreviewSurface can't express) are
  not listed. "Object/parameter introspection" below lists the places where
  the device honours less than the schema states.

Instances and attributes:

- An instance `transform` array also accepts `FLOAT32_MAT3x4` elements. The
  schema lists `FLOAT32_MAT4` only, and VisRTX rejects them, so a scene that
  uses them won't load elsewhere.
- Short arrays are tolerated rather than rejected. An instance `id` array
  shorter than the transform array falls back to the scalar `id` for the
  missing elements, and a short attribute array reads the ANARI default
  `(0,0,0,1)` for them; both warn.

Samplers and normal maps:

- A channel-mixing `outTransform` is baked into a copy of the image per
  sampler, so N mixing samplers over one image hold N decoded copies.
- An image-only array edit on a sampler whose transform is baked re-authors
  every surface that uses it. A translation in the fourth column of
  `inTransform` is always baked, although it could be expressed as a texture
  transform.
- The normal-map tangent frame comes from the geometry's `attribute0` (`st`),
  whatever the sampler's `inAttribute` is. `vertex.tangent` is not read, and
  geometry without an `st` that varies across the surface (no `attribute0`,
  tessellated spheres) gets no normal mapping at all. VisRTX uses
  `vertex.tangent` or an arbitrary frame around the geometric normal instead,
  so maps on such geometry differ from VisRTX.
- On cones, cylinders and curves, `st` varies in one direction only, so the
  normal-map result there is unspecified and untested.
- Where the tangent frame or the map value is degenerate, the shading normal
  is kept. VisRTX uses the geometric normal.
- Delegates other than Storm ignore normal maps without a warning.

Backgrounds:

- A `background` image is composited under the color on the CPU each time
  `channel.color` is mapped, so it appears in `channel.color` only (the other
  channels keep their miss values) and costs a bilinear lookup per pixel per
  map. It is claimed on HdEmbree too, where its interaction with a dome
  light and with transparency is untested.
- A one-channel background image reads as `(r, 0, 0, 1)`, where VisRTX gives
  it alpha 0. VisRTX's `premultiplyBackground` is not supported. Unsupported
  element types are warned about and ignored.

Volumes and spatial fields:

- Every field is decoded into a dense float32 grid. A `nanovdb` field becomes
  a dense grid over its whole active bounding box, so a large sparse VDB can
  exhaust host or GPU memory; there is no size guard, and Storm's
  texture-memory budget is not applied. Only `Float` and `Fp4/8/16/N` NanoVDB
  grids with an axis-aligned transform are supported. OpenVDB-backed fields
  are future work.
- VisRTX's rectilinear field types (`structuredRectilinear`,
  `nanovdbRectilinear`) are not implemented.
- Volumes are emission-absorption only: lights and the ambient fill don't
  shade them, while VisRTX shades volume albedo. Volumes also write nothing
  to the ID channels, so they can't be picked, and the volume `id` is
  ignored.
- Volumes and isosurfaces have been checked against analytic fields, but not
  compared against VisRTX at runtime. Known differences (transfer-function
  binning, lighting, how overlapping volumes composite) are documented below
  but not measured.

Isosurfaces:

- Isosurfaces are flat-shaded, so facets show at grid resolution. The device
  never generates normals, while VisRTX shades isosurfaces with the field
  gradient.
- The surface is the level set of a tetrahedral split of each cell, while
  volumes sample trilinearly, so an isosurface and a volume of the same field
  don't line up exactly (the difference is second order in the grid
  spacing).
- There is no limit on the number of isovalues (VisRTX caps it at 128). A
  `primitive.id` array shorter than the isovalue list only warns, while short
  `primitive.color` and `primitive.attributeN` arrays are dropped.
- Extraction runs single-threaded on the device worker, and any edit
  re-extracts the whole field. There is no size guard and no setting to
  trade accuracy for cost.
- No test builds an isosurface from a `nanovdb` field.

Bugs in other delegates:

- HdEmbree: after an in-place edit of a visible hdri light, HdEmbree adds the
  re-synced dome to its camera-visible list again, so the backdrop shows two
  copies summed. Lighting is correct.
- hdGatling compares dome lights by pointer, so an in-place hdri edit (its
  `radiance` image or `scale`) does not show until the frame is recreated.

## Implementation notes

The rest of this README records how the device maps each part of ANARI onto
Hydra, the costs involved, and which tests cover it. Source paths are
relative to this directory.

- **Delegate-agnostic subtypes** — `DelegateRegistry` enumerates the
  Hydra renderer-plugin registry; each delegate is an ANARI device subtype
  (`HdStormRendererPlugin` → `storm`), plus a `default` alias.
- **Worker thread ownership** — one `Worker` per device owns the Hgi/GL
  context and every `HdEngine::Execute`; app calls post jobs. The
  one-Hydra-thread rule holds by construction.
- **Zero-copy mirror** — one `HdRetainedSceneIndex` per
  world; `ZeroCopyDataSource` serves ANARI array memory as a foreign-data
  `VtArray`; the `MirrorDirtySet` is the sole stage-1 → stage-2 interface; the
  render prologue drains it into `AddPrims`/`DirtyPrims`/`RemovePrims`. The
  `VtArray`'s foreign source is a separate refcounted object that owns the
  ANARI array reference, so it outlives a rebuild that replaces the data
  source: delegates may keep a pulled array across Syncs (HdEmbree's mesh
  keeps its points), and freeing the source with the data source crashed
  HdEmbree on the next Sync (`render_geometry_edit`, storm + embree).
- **Incremental invalidation** — geometry array members are `ChangeObserverPtr`s,
  so an in-place `anariUnmapArray` re-finalizes the geometry, marks the
  mirror-dirty set, and the prologue emits a locator-precise `DirtyPrims(points)`
  with no topology resync (verified in the render test: coverage drops on shrink,
  no rebuild). The render prologue skips the reconcile entirely when nothing was
  marked.
- **Frame lifetime** — render jobs self-anchor an `IntrusivePtr<Frame>` so an
  `anariRelease(frame)` before the queued job runs cannot free it (a per-job form
  of the worker-anchor guarantee); teardown runs on the worker. Validated
  clean under AddressSanitizer.
- **Object model** — triangle/quad geometry (→ mesh), sphere (→ tessellated mesh),
  curve (→ linear basisCurves; `primitive.index` authors per-segment
  `curveVertexCounts`/`curveIndices`), cone/cylinder (→ commit-time-tessellated
  mesh, with `caps` end-disk triangle fans on ends with radius > 0),
  isosurface (→ commit-time-extracted mesh, "Isosurfaces" below),
  matte/physicallyBased materials, surface, group, instance, world, perspective/orthographic
  camera, renderer-as-settings. Non-mesh and tessellated rprims rebuild on any
  change; the zero-copy DirtyPrims(points) fast path stays scoped to plain
  meshes.
- **App-provided mesh shading data** — triangle/quad geometry
  preserves `vertex.normal` and `faceVarying.normal` as the `normals` primvar
  with role `normal` and the corresponding interpolation. Face-varying data
  is ordered by face corner, independently of position indices, and takes
  precedence over vertex data. The same mapping supports `faceVarying.color`
  and `faceVarying.attribute0..3`, including `st` texture coordinates from
  attribute0. Normals are app-provided only; the device never generates them.
  These arrays are observed and decoded into copied primvars, so edits rebuild
  the prim rather than taking the points-only fast path. Removing the last
  copied array also rebuilds once to remove the old primvar. `render_normals`
  tests supplied normals and in-place mutation at both interpolation rates;
  `render_sampler` tests indexed face-varying UVs and live UV edits.
- **Instancing via HdInstancer** — one instancer per (world, group)
  at `/anari/w{N}/instancers/g{G}`, with the group's surfaces as its prototype
  rprims `/anari/w{N}/instancers/g{G}/s{S}` each
  referencing the instancer via `instancedBy`. The instancer carries a per-
  instance `instanceTransforms` primvar; every instance draws every prototype.
  Instances of a group aggregate onto its single instancer (N instances → the
  prototypes + N transforms, not N baked rprim copies). Verified: covered area
  scales with instance count under Storm. `instance.transform` accepts a
  FLOAT32_MAT4, the compact FLOAT32_MAT3x4 (implied bottom row restored), or an
  ARRAY1D of either (`KHR_INSTANCE_TRANSFORM_ARRAY`): `World::gatherInstancedGroups`
  flattens an array instance to one instancer entry per element, with `id`
  (UINT32, or a parallel UINT32 array that is honoured only alongside a
  transform array, per the schema and VisRTX; a short array falls back to the
  scalar with a warning) and the uniform attributes (below) indexed by the
  same element, so scalar and array instancing author the same
  instancer and render byte-identically (asserted in `render_instance_array`,
  for surfaces, two groups, and group lights). Expanded lights of an array
  element live at `/anari/w{N}/lights/i{I}_t{k}_l{L}`. The instance arrays are
  held through `ChangeObserverPtr`, so an in-place map/unmap edit or an array
  swap of another length re-syncs the instancer. Both `KHR_INSTANCE_TRANSFORM`
  and `KHR_INSTANCE_TRANSFORM_ARRAY` are advertised, and the instance
  parameter list is reflected through `anariGetObjectInfo`. Accepting MAT3x4
  array elements is a documented superset of the schema, which lists MAT4 only.
- **Material networks** — the device authors a UsdPreviewSurface
  `HdMaterialNetworkSchema` per material (one static topology, per-material params
  poked in) as a `material` sprim under the universal render context, and binds
  it on the rprim via `HdMaterialBindingsSchema`. `matte` maps color→diffuseColor
  (metallic 0/roughness 1); `physicallyBased` maps the core table
  (baseColor/metallic/roughness/emissive/ior/clearcoat/occlusion, alphaMode→
  opacity/opacityThreshold) and warns about and drops the inputs UsdPreviewSurface cannot express. The mirror runs
  through the delegate's registered scene-index filter chain
  (`AppendSceneIndicesForRenderer`) so binding/node-identifier resolution matches
  UsdImagingGLEngine. Verified: the network reaches Storm as a non-fallback
  UsdPreviewSurface `HdStMaterial` with all params, and network-only constant
  params (emissiveColor) visibly shade the draw (asserted in `render_material`).
- **Lights** — the six ANARI light subtypes map to UsdLux sprims
  (directional→distant, point→sphere, spot→sphere+shaping cone, quad→rect,
  ring→disk, hdri→dome) authored under `/anari/w{N}/lights/l{L}` with a
  radiometric approximation (exposure pinned 0). A world with ANARI lights drops
  the fallback headlight (kept only for lightless worlds) to avoid double-lighting.
  Light parameter values (intensity/color) scale the render (asserted in
  `render_lights`).
- **Instanced-light expansion** — a light placed in a `group` expands to
  one sprim per (instance, light) pair, joining the serial-ID taxonomy at
  `/anari/w{N}/lights/i{I}_l{L}` (instance serial I × light serial L) alongside
  world-direct `l{L}` lights. `Group::lights()` collects the group's lights;
  `gatherInstancedGroups()` carries per-instance (serial, transform) parallel to
  the surface instancer's transforms; `Mirror::syncLights` composes each light's
  local transform with its instance transform (`lightData.xform * instanceXform`,
  the GfMatrix row-vector convention) and re-authors the full world-direct +
  expanded set each sync — so parameter changes fan out to all
  expanded sprims with no separate dependent-prims map. N instances × M lights =
  N×M sprims (linear, uncapped — perf out of scope). A world whose only lights are
  instanced-group lights still counts as lit (`World::hasAnyLight()`), so the
  headlight drops. Asserted in `render_instanced_lights`: one point-light group
  instanced at ±X lights both sides of a frame-filling quad — adding the second
  instance brightens the far region a lone light leaves dark (left 40→247).
- **Ambient fill** — the `default` renderer's core `ambientRadiance`
  (float) / `ambientColor` (float3) inject a flat, shadowless fill by synthesizing
  one uniform dome-light sprim whenever `ambientRadiance > 0`. The fill rides
  the dome's texture: a 1×1 float env image of `ambientColor * ambientRadiance`
  served through the in-memory `anari://renderer/ambient_f{F}_r{R}.anari` scheme,
  with the dome's own `intensity`/`color` left at 1, because Storm ignores a
  textureless dome and HdEmbree's dome ignores `intensity` and `color` (it
  lights with the texture alone). Each change gets a fresh path, since
  delegates cache textures by path. Claimed as `KHR_RENDERER_AMBIENT_LIGHT`
  wherever the delegate has dome lights (Storm and HdEmbree). The
  sprim lives in a small *frame-owned* retained scene index at `/anari/frame/ambient`
  (inserted alongside the world mirror) — ambient is a renderer/frame concern, not
  a world one — authored/dirtied/removed as `ambientRadiance` crosses 0 or its
  color/intensity change. Scene-ambient via `GlfSimpleLightingContext` does *not*
  reach UsdPreviewSurface under Storm (checked in a prototype), hence a real dome-light sprim.
  Ambient counts as app-supplied lighting, so it drops the fallback headlight:
  `headlight only when !hasAnyLight() && ambientRadiance == 0`.
  Asserted in `render_ambient` (storm + embree): a white quad grazed by its only
  light so its front face is dark lifts to bright with `ambientRadiance 2`; a red
  `ambientColor` tints the fill red; in a float frame `ambientRadiance` 0.5 fills
  twice as bright as 0.25; a lightless world with ambient still renders visible.
- **Background clear color** — the `default` renderer's core `background`
  (FLOAT32_VEC4) becomes the color AOV's clear value: the color shown where
  primary rays miss geometry. Applied each render as the color AOV's `clearValue`
  via `SetRenderOutputSettings(HdAovTokens->color, …)` (started from the delegate
  default descriptor) — driven off its own path, not the `SetRenderOutputs`
  channel-set guard, so a `background` change with no channel-set change still
  lands, and it survives the guarded `SetRenderOutputs` recreating the color
  binding. A dome light OWNS the backdrop under Storm (`HdxSkydomeTask` is the
  first render task and paints the dome env into primary rays), so the clear color
  only shows once the skydome is suppressed. The global render setting
  `HdRenderSettingsTokens->domeLightCameraVisibility` is the lever — set `false`,
  the skydome is suppressed while the dome's light contribution to surfaces is
  preserved (there is no per-dome camera-visibility in Hydra's public API).
  Policy, pushed each frame after the renderer-settings loop (so it wins any
  verbatim renderer-setting passthrough of the same token): **no hdri dome present → `false`**
  (covers the no-dome and ambient-only cases — the synthetic ambient fill dome is
  never an hdri, so it is always camera-invisible and `background` owns the
  backdrop), **hdri present → follow the first hdri's `visible`** (ANARI core light
  bool, default true; `Light::isHdri()` + `Light::visible()`), so a default hdri
  stays the backdrop while `visible=false` reveals `background`. Multiple hdri →
  first wins + warn; ambient+hdri → the first hdri's `visible` wins globally
  (one lever, one representative skydome). Asserted in `render_background` (storm):
  a blue `background` shows in the corners with no dome and stays blue under an
  ambient dome (the crux — dome camera-invisible) while the quad lifts; a red hdri
  paints the corner red when visible and reveals blue when `visible=false` (quad
  still hdri-lit); and a blue→green `background` change with no channel-set change
  follows in the backdrop.
- **Background image (`KHR_RENDERER_BACKGROUND_IMAGE`)** — `background`
  given as an `ARRAY2D` (any of the extension's UFIXED8/16/32, sRGB and FLOAT32
  element types; others are warned and ignored) is decoded to linear RGBA in
  `Renderer::finalize` through the shared `DecodeArray2DToImage` (so an in-place
  map/unmap edit, which re-finalizes the renderer as an array observer, is
  followed) and published as an immutable snapshot. Each render the frame takes
  that snapshot and clears the color AOV to transparent black instead of the
  constant colour; `readbackColor` then composites the image *under* the
  rendered, premultiplied colour in float before any 8-bit/sRGB encode, matching
  VisRTX's composite: `rgb = C.rgb + (1 − C.a)·bg.rgb`, `a = C.a + (1 − C.a)·bg.a`
  (background not premultiplied, alpha carried through, HDR texels unclamped in
  a float channel). Sampling also follows VisRTX: stretched over the frame
  (aspect not preserved), `uv = (pixel + 0.5) / size` with image row 0 on frame
  row 0 (ANARI's bottom row), bilinear, clamp-to-edge. There is no screen-space
  Hydra task: the composite is delegate-independent, so the extension is claimed
  on every delegate. A visible hdri's skydome writes opaque alpha and therefore
  hides the image; `visible=false` reveals it (the dome policy above is
  unchanged). Setting a `FLOAT32_VEC4` again returns to the clear-colour path.
  Limitations: the composite reaches `channel.color` only (other channels keep
  their miss values); and it costs one bilinear lookup per pixel per `map`.
  `_SRGB` element types decode with the exact IEC 61966-2-1 curve and keep
  alpha linear (helium's decoder, shared with samplers).
  Cross-checked at runtime against VisRTX with the same test scene: its misses
  match the reference sample within its 8-bit filter-weight error (≤0.004) for
  float and UFIXED8_RGBA_SRGB images, confirming orientation, stretch, bilinear
  filtering and linear `_SRGB` alpha. One deliberate difference: a one-channel
  image reads as `(r, 0, 0, 1)` here (ANARI attribute convention) where VisRTX's
  CUDA fetch yields alpha 0.
  Asserted in `render_background_image` (storm + embree): a 4×3 asymmetric
  image on a 96×48 frame matches a reference bilinear sample at every miss pixel
  (float HDR/alpha, sRGB-8 output, FLOAT32 and UFIXED8_RGBA_SRGB images), an
  opaque bottom-left quad covers it, an in-place edit is followed, a constant
  background replaces it; on Storm a blended half-opaque quad composites over
  it, a visible hdri hides it and `visible=false` reveals it.
- **Lighting enabled at the draw** — `HdxRenderTaskParams::enableLighting`
  defaults to *false*, under which `HdStRenderPassState` swaps the simple-lighting
  shader for its fallback "fully lit" shader and sprim shader-uniform data
  (material constant params, light params) syncs correctly but never reaches the
  draw. The frame pushes render params with lighting on (as UsdImagingGLEngine
  does), which resolved the formerly documented "shared shader-uniform gap".
  HdEmbree has the same switch as its own `enableLighting` render setting
  (default false: every light sprim is ignored and a camera light shades
  instead). The frame sets it to whether the world has lights or an ambient
  fill, so a lit world uses its ANARI lights and a lightless one keeps the
  camera light, as Storm keeps its headlight; an app that sets
  `enableLighting` itself (renderer-setting passthrough) wins. Asserted on embree by
  `render_hdri` and `render_ambient`.
- **displayColor fallback** — constant material color authored as
  `displayColor`/`displayOpacity` so delegates that ignore/incompletely bind
  material networks (Embree) show scene color.
- **Samplers / textures** — `image2D` and `image1D` (as N×1) are
  real objects; a material input set to a sampler authors a `UsdUVTexture` (+ a
  per-binding `UsdPrimvarReader_float2` feeding `st` from the sampler's
  `inAttribute`) into the network, and the texture visibly shades under Storm
  (asserted in `render_sampler`, a 2×2 checkerboard read per-quadrant). The image is served
  from memory through the **in-memory asset scheme**: an `AnariImageRegistry`
  keyed by an `anari://…​.anari` path plus a self-registered `HioImage` plugin
  (`scene/AnariImage.cpp`, compiled into the device library, `plugInfo.json`
  in an `anari_hydra/` directory beside it, registered at device init). Only
  that directory is registered, so other packages' manifests in a shared
  library directory (hdAnari's, for one) are not loaded; `render_sampler`
  re-runs from such a directory with a decoy manifest to check this. Delegates that load textures
  through Ar with their own decoders (hdGatling) instead reach the registry via
  the `anari` Ar URI resolver (`scene/AnariArResolver.cpp`, same `plugInfo.json`),
  which encodes each image as an in-memory TGA (8-bit) or EXR (half/float, and
  every dome image). Sampler finalization
  advances a revision included in every consuming material's mirror signature.
  It decodes the current image and versions its asset path, retiring the previous
  registry entry, so parameter commits, image replacement, and image unmap edits
  reach Storm without recommitting materials. Each frame tracks the device's
  mirror revision so one frame cannot consume another world's updates.
  Unchanged frames neither decode images nor re-author networks. A sampler
  recommit currently refreshes its image asset even if only a filter or transform
  changed. `render_sampler` covers live edits and sharing across frames/worlds.
  `wrapMode1/2` map to
  UsdUVTexture `wrapS/wrapT`. **Coordinates and transforms**:
  `inAttribute` picks `attribute0..3`/`color` or a geometry-derived source
  (`objectPosition`/`worldPosition`/`objectNormal`/`worldNormal`/`primitiveId`,
  `Sampler::kSlot*`; these always take the baked path below, sourced by
  `PrimBuilder` from the authored points / app-provided or tessellator normals /
  the Hydra-face→ANARI-primitive map, with the rprim `xform` applied for the
  world variants — identity for world-direct surfaces; an instanced surface
  reading a world variant is authored as per-instance copies instead of a
  prototype, see "Per-instance surface copies" below). Unknown names warn and read as an
  absent attribute — `Sampler::kAbsentAttributeSlot` sends the reader to the
  never-authored `anari_absent_st` primvar and `PrimBuilder` bakes nothing, so
  both paths hit the same `(0,0)` reader fallback as an unauthored
  `attributeN`; every binding authors its own
  `UsdPrimvarReader_float2` (never shared — Storm's network lowering visits a
  reader once, so a reader feeding two `UsdTransform2d` nodes would leave the
  second without coordinates) on `st` (attribute0) or the `attributeN_float2` /
  `color_float2` aliases `PrimBuilder` authors (and the instancer mirrors for
  per-instance overrides; for the baked path below the instancer also authors
  each baked `st_s{N}`/`val_s{N}_*` primvar at instance rate, the sampler's
  map of each instance's value, via `BuildDerivedInstanceAttributes`). `Sampler::commitParameters` classifies
  `inTransform`/`inOffset`: a rotation·scale 2×2 block (orthogonal columns) with
  no z/w-column terms becomes a `UsdTransform2d` (`rotation`/`scale`/
  `translation`, with the ANARI→USD row flip `v -> 1-v` folded in as
  `rot = −θ, scale = (sx, −sy), translation = (bx, 1−by)`); anything else
  (shear, z/w-column terms such as a translation in the fourth column) is
  *baked*: `BuildSurfacePrim` collects the material's baked samplers into
  `DerivedPrimvar` entries and `GatherOptionalPrimvars` authors a derived
  `st_s{serial}` float2 primvar per sampler holding
  `inTransform·(x,y,0,1)+inOffset` flipped to USD space, at the source's own
  interpolation; `Mirror::PrimRecord::derivedSig` carries those samplers'
  revisions so a transform edit re-authors the rprim (and a position source
  disables the points fast path).
  `outTransform`/`outOffset`: diagonal → texture `scale`/`bias`; any channel
  mixing/swizzle is baked per texel into the sampler's own registry image
  (`TexelAffine` in `DecodeArray2DToImage`, stored in float so the result may
  leave `[0,1]`), so color and scalar slots (which
  read `r`) see exact mixing, including glTF's packed opacity/metallic/
  roughness selection. Asserted in `render_sampler_transforms` (vertex and
  face-varying): two samplers on different attributes, 90° rotation with the
  translation in `inOffset` and in the matrix's fourth column, shear, reflection
  + non-uniform scale, z-column padding, R/B swap, R+G mixing with an offset,
  scalar swizzle/mixing, nearest vs linear through both paths, `image1D` on
  attribute1, and live edits/unsets of all of it.
  **`transform` and `primitive` samplers** have no texture: they ride
  the same `DerivedPrimvar` machinery. A transform sampler bakes
  `outTransform·attr+outOffset` into `val_s{serial}_float3` (color slot) /
  `_float1` (scalar slot, first component) at the source's interpolation
  (a constant `(0,0,0,1)` image when the geometry lacks the source); a
  primitive sampler bakes `array[primitiveId+inOffset]` (`(0,0,0,1)` past the
  end) as a uniform primvar through the geometry's primitive map, so
  tessellated spheres and curve segments index by ANARI primitive and a shared
  material bakes per geometry. `MaterialBuilder` reads either with a
  `UsdPrimvarReader_float3`/`_float` connected straight to the surface input.
  On the `normal` slot either feeds a generated surface shader instead (see
  "Normal-map material input").
  Asserted in `render_sampler_derived` (storm; two quads with a reversed
  `primitive.index` plus two tessellated spheres sharing one material): lookup
  by ANARI primitive rather than vertex order, `inOffset`, out-of-range
  reads, in-place and replacement array edits, scalar opacity, transform
  samplers on objectPosition x/y/z (the sphere reads its surface z),
  worldPosition, objectNormal/worldNormal (absent → app-provided →
  removed; tessellated normals), primitiveId and a vertex.color swizzle,
  image samplers on primitiveId and objectPosition, and unbinding.
  `image3D` is the remaining documented exclusion (inert `UnknownObject` +
  warn). The `hdri` dome light's environment image
  rides the same scheme: its `radiance` array decodes (via the shared
  `DecodeArray2DToImage`) into the registry under `anari://light/l{N}.anari` and
  the dome's `textureFile` (`SdfAssetPath` with both authored+resolved set so
  Storm's dome path, which reads the resolved field directly, dispatches Hio by
  extension with no Ar round-trip) points at it — Storm samples the real texture
  instead of the constant-color fallback (asserted in `render_hdri`, a
  surface-tint dominance check plus the HDR radiance check below). The light's
  `scale` and `color` are folded into the decoded texels (a `TexelAffine`, so a
  non-unit gain stores Float) and the dome's `intensity`/`color` stay 1, since
  HdEmbree's dome ignores both; every decode gets a fresh
  `anari://light/l{N}_r{R}.anari` path so an in-place edit reloads. HdEmbree
  reads the image through the same Hio plugin, asking for linear
  `Float32Vec3`, which `AnariHioImage` converts to (Storm still gets the stored
  bytes). `render_hdri` (storm + embree) checks scale × color and an in-place
  `scale` edit. Two delegate bugs show up after an in-place hdri edit:
  HdEmbree re-adds a re-synced dome to its camera-visible list, so a visible
  dome's backdrop sums two copies (lighting is right); hdGatling compares
  dome lights by pointer, so a re-created dome at the same address keeps the
  old texture until the frame is recreated.
  **Image precision**: `AnariImageData` stores texels in one of four
  layouts (`TexelFormat::UNorm8`/`Srgb8`/`Half`/`Float`) and
  `AnariHioImage::GetFormat` reports the matching
  `HioFormatUNorm8Vec4`/`UNorm8Vec4srgb`/`Float16Vec4`/`Float32Vec4`, which HdSt
  uploads unconverted (`textureUtils.cpp` requests exactly `GetFormat()`).
  `TexelFormatFor(elementType, transformed)` picks the layout: 8-bit linear
  sources stay UNorm8 (bit-exact); 8-bit sRGB `R`/`RGB`/`RGBA` sources keep
  their bytes in Srgb8, an sRGB-typed texture the GPU decodes (colour through
  the sRGB curve, alpha linear, as helium does; same 4 bytes per texel, as in
  VisRTX), so dark codes are not re-quantised; `RA_SRGB`, whose linear alpha
  helium puts in green, is linearised into half; and `UFIXED16`/`UFIXED32`/`FLOAT32`
  sources or any `TexelAffine`-baked image go to float32 (exact; HDR and
  signed values survive). `AnariImageData::read` decodes Srgb8, so CPU readers
  (the background composite) see linear values. `DecodeArray1DToImage` gives
  image1D the same path.
  The hdri dome decodes its image the same way, so HDR dome radiance
  survives: `render_hdri` checks a 4× dome lights a surface 4× a 1× dome, and
  a dark sRGB dome against the exact decode. An earlier 8-bit dome cap hid
  "Storm garbage" with float environments that was really `render_background`
  handing the device a shared array whose `std::vector` was freed before the
  deferred decode at render time (glibc's tcache pointer and key in the first
  texels); keep shared array memory alive for the array's lifetime.
  Asserted numerically in `render_image_precision`: an emissive-textured black
  quad rendered into a FLOAT32_VEC4 frame reads back texels of 4.0, -0.5,
  0.001, 100, a 32-step gradient of 1/4096, sRGB codes 2/5/10/128, UFIXED16
  codes 500/504/508, image1D HDR texels, a `2r - g` mix of 1.498, and live
  unmap edits plus replacement by images of other types and sizes. The
  `hydra.textureMemory` frame property (Storm's `GetRenderStats`
  `textureMemory`, stamped per render like `duration`) shows sRGB `R`/`RGB`/`RGBA`
  images costing exactly what an 8-bit linear image of the same size does, and
  `RA_SRGB` more. The Hio
  plugin's target-memory downscale path (nearest-neighbour) is format-generic
  but untested: `UsdUVTexture` `textureMemory` is never authored, so Storm
  always requests full resolution.
- **Normal-map material input (`normal`→`normal`)** — a
  `physicallyBased` material's `normal` set to a sampler authors a `UsdUVTexture`
  connected to the surface `normal` input, so a tangent-space normal map perturbs
  shading under Storm. It rides the same sampler→`UsdUVTexture` path as every
  other texturable slot (`SamplerBinding::normalMap`), with one difference: as
  in VisRTX (`sampleNormalMap`: `evaluateSampler(...) * 2 - 1`), the sampler's
  full `outTransform`/`outOffset` applies first and the `[0,1]`→`[-1,1]` unpack
  after it. A diagonal transform composes into the texture's output
  `scale`/`bias` (`2·scale`, `2·bias − 1`); a channel-mixing one is already
  baked into the texels. `sourceColorSpace` is already `raw` (correct — a
  normal map must not be sRGB-decoded). Transform and primitive samplers bind
  too, through a generated shader: Storm's UsdPreviewSurface perturbs the
  normal only when `normal` comes from a texture (`perturbFragmentNormal`
  needs `HD_HAS_COORD_normal` and builds the tangent frame from that texture's
  `st`), so it would ignore their baked `val_s{N}_float3` primvar. A material
  with one on `normal` adds a Storm-only network under the `glslfx` render
  context (the universal one leaves the binding out). Its surface node
  (`MaterialBuilder`, registered with Sdr from source once per process)
  imports Storm's `previewSurface.glslfx` and swaps only the normal-mapping
  section: it unpacks the packed primvar itself (`v * 2 - 1`, so the same
  sampler on a color slot stays packed) and builds the frame from the
  geometry's `st` through an extra `anariTangentSt` reader input. Without an
  `st` varying across the surface (no attribute0, or a tessellated sphere,
  whose attribute0 is per sphere) the frame degenerates and the map has no
  effect; on cones, cylinders and curves `st` varies along one direction only
  and the result is unspecified. An image map behaves the same. Cost: one Sdr
  node per process; one Storm program per material/primvar layout, reused
  across `outTransform`, `inAttribute` and `array` edits (counted with
  `TF_DEBUG=HDST_LOG_DRAWING_SHADER_PROGRAM_MISSES`); a primvar-layout change
  (adding/removing `st`) or instancing compiles a new one, as for any
  material. **No geometry change**: Storm synthesizes the per-vertex TBN
  frame from `st` (no `tangents` primvar needed) — checked first in a
  prototype (a plain `st`-only quad's normal map perturbs shading; a flat
  control does not). Only `physicallyBased`
  carries `normal` (matte has no such slot); a non-sampler `normal`
  (constant/attribute) has no faithful UsdPreviewSurface target and is
  warn-and-dropped, falling back to the geometric normal. Asserted in
  `render_normal_map` (storm): two quads under a single directional light from
  the +X side, compared with a flat (no normal map) render. A two-tone image
  map (−X tilt left, +X tilt right) darkens the left half and brightens the
  right; a diagonal `r' = 1 − r` outTransform and an equivalent channel-mixing
  one both swap the bright half; transform and primitive samplers on `normal`
  perturb the same way, and their `outTransform`, `inAttribute` and `array`
  edits swap it; the transform sampler still perturbs on an instanced surface
  and has no effect on a geometry without `st`; and the same image or
  transform sampler on `baseColor` is not unpacked.
- **Attribute-bound material inputs** — every UsdPreviewSurface parameter
  slot uniformly accepts constant / sampler / **attribute**: a material input set
  to an ANARI attribute STRING (`color`, `attribute0..3`) authors a
  `UsdPrimvarReader_float3` (color3f slots: diffuseColor/emissiveColor) or
  `UsdPrimvarReader_float` (scalar slots: opacity/metallic/roughness/occlusion)
  whose `varname` reads the matching per-vertex primvar, connected to the surface
  input. Each subtype tries sampler, then attribute (`bindAttribute` via
  `hasParam(name, ANARI_STRING)`), then the constant read; unrecognized attribute
  strings (worldPosition, primitiveId, none, …) warn and fall through to the
  constant. `PrimBuilder` authors the readable per-vertex primvars — `color`
  (float3, from vertex.color) and `attribute0..3` (natural component count, from
  vertex.attributeN), plus typed scalar/color aliases for material readers.
  Primitive arrays and constant float4 values supply lower-precedence fallbacks.
  Primvars are authored only when the geometry carries a value, so an
  unsatisfied reader returns its fallback (the `st` primvar for samplers is
  unchanged). The material signature (`Mirror::MakeMatRecord`) includes the
  attribute bindings so binding/unbinding re-authors the network. Asserted in
  `render_attribute` (storm): a triangle with red/green/blue corner colors and a
  matte `color` bound to `"color"` renders a visible gradient — each corner's own
  channel dominates its region, which a constant color cannot produce.
- **Per-instance material/attribute overrides** — an ANARI `instance`'s
  per-instance uniform attributes (`color`, `attribute0..3`) feed the prototype
  surfaces' material attribute reads *per instance*, so N instances of one group
  shade differently. Each accepts FLOAT32 / VEC2 / VEC3 / VEC4 (missing lanes
  take the ANARI default `(0,0,0,1)`) or an ARRAY1D of any attribute element
  type indexed by transform element (element 0 under a scalar transform;
  elements past the array end read as the default, with a warning).
  `Instance` collects them into `InstanceOverrides` (present-
  flagged so an unset attribute doesn't override a sibling); `World::InstancedGroup`
  carries them parallel to the instance transforms; `Mirror::BuildInstanceAttributes`
  decodes them into instance-rate primvar arrays (`color` float3, `attribute0..3`
  float4) authored on the (world, group) instancer by `InstancerBuilder`, named to
  match the per-vertex primvars the existing attribute-bound readers already
  read — so **no new material machinery**. An array covers every instance, so an
  attribute is authored only when at least one instance sets it, unset instances
  filled with the ANARI default (0,0,0,1); this fill is safe because a prototype's
  own per-vertex primvar wins over the inherited instance-rate one (ANARI precedence
  per-vertex > per-instance-uniform, **checked first in a prototype**:
  an instance-rate `color` resolves into the prototype's `UsdPrimvarReader` through
  `AppendSceneIndicesForRenderer` under Storm, and a per-vertex `color` on the
  prototype overrides it). `InstancerRecord::attributes` compares the arrays by
  value so an override change re-authors the instancer. Asserted in
  `render_instance_override` (storm; embree checks coverage and instance IDs
  only, as HdEmbree has no attribute-driven materials): one quad group
  instanced twice with red/green
  `instance.color`, matte `color` bound to attribute `"color"`, no per-vertex color
  — the left screen third renders red, the right green (a single shared material
  cannot make two regions favor different channels).
- **Renderer settings push** — the `renderer` distils its
  parameters into a Hydra render-setting map (a small core translation table —
  `pixelSamples` → `convergedSamplesPerPixel` — plus verbatim delegate-native
  passthrough, translation winning collisions) that the frame applies both at
  delegate creation (`CreateRenderDelegate`'s `HdRenderSettingsMap` overload, so
  creation-time-only settings land) and per-frame via
  `HdRenderDelegate::SetRenderSetting` (settings that change after creation).
  Verified on Embree (`render_settings`): toggling `enableSceneColors` flips a
  red quad between red and white. The sample budget always reaches the
  delegate: `pixelSamples` advertises default 1 / minimum 1 (favoring
  interactivity over a delegate's own budget, e.g. HdEmbree's 100), unset means
  1, and a value below 1 warns and falls back to 1 — a zero budget, which an
  app zero-filling an introspected parameter without a default sends, made
  HdEmbree report converged on a background-only frame. Asserted in
  `render_settings` (unset and `pixelSamples=0` both render the quad) and
  `query_object_info` (default and minimum).
- **Renderer settings reflection** — the capability probe also captures
  the bound delegate's `GetRenderSettingDescriptors()`; each becomes a real,
  typed ANARI renderer parameter under its verbatim Hydra token, discoverable
  through core introspection — `getObjectInfo(ANARI_RENDERER, "default",
  "parameter", ANARI_PARAMETER_LIST)` returns the bridge's own renderer parameters
  from the generated introspection table (`pixelSamples` INT32, `background`
  FLOAT32_VEC4, `ambientRadiance`, `ambientColor`) followed by every delegate
  render-setting token verbatim (VtValue→ANARIDataType: bool→BOOL, int→INT32,
  unsigned→UINT32, float/double→FLOAT32, string/TfToken→STRING, GfVec2/3/4f→
  FLOAT32_VEC2/3/4; unmapped types dropped + warned). On a name collision the
  alias wins: a token a core alias maps to (e.g. `convergedSamplesPerPixel`) is
  not emitted verbatim. `getParameterInfo` returns each reflected setting's
  `default` value, human-name `description` and `required` (false). The cached
  list + backing strings live in `HydraGlobalState` so the returned pointers stay
  valid. Asserted in
  `reflect_settings` (storm + embree, non-GL): `pixelSamples`/`background`
  present and typed; embree's `enableSceneColors` (BOOL) and
  `ambientOcclusionSamples` (INT32) reflected verbatim while
  `convergedSamplesPerPixel` is hidden by the alias.
- **Object/parameter introspection** — the tables behind
  `anariGetObjectInfo`/`anariGetParameterInfo` are the **vanilla KHR schema**:
  `introspection/hydra_device.json` (the device root) names the ANARI-SDK's
  own shipped `khr_*.json` files (`share/anari/code_gen/api`, never copied or
  trimmed) plus one local file, `hydra_bridge_parameters.json`, for what the
  device adds beyond KHR (`hydra.clippingRange`, hdri `visible`, renderer
  `pixelSamples`, geometry `primitive.id`,
  instance `transform` as `FLOAT32_MAT3x4` and scalar/vec2/vec3/array
  attribute overrides, physicallyBased `occlusion` as constant/attribute). The
  SDK's `anari_generate_queries` merges them into `anari_library_hydra_queries.cpp`
  at build time — the VisRTX pattern. The SDK merge keeps the first-seen entry
  for a parameter name (description, default) and appends later `types`/`values`;
  each appended type keeps the source extension of the file that added it, so a
  bridge form of a KHR name (instance `transform` MAT3x4, the narrower
  `color`/`attributeN` widths, `occlusion` constant/attribute) carries no source
  extension, just like the genuinely new names (`hydra.clippingRange`,
  `visible`, `primitive.id`, `pixelSamples`), while `background` ARRAY2D reports
  `KHR_RENDERER_BACKGROUND_IMAGE` and instance `transform` ARRAY1D
  `KHR_INSTANCE_TRANSFORM_ARRAY` (the query generator records the source
  extension per parameter type). The bridge file is listed
  last in the root's dependencies so the KHR wording is the first-seen
  description. hdri `visible` stays a bridge
  parameter rather than `KHR_LIGHT_PRIMARY_VISIBILITY` because the device
  applies it to the dome only, not to the other five light subtypes. The only narrowing at query time
  (`Introspection.cpp`) is extension existence: a subtype the delegate cannot
  create answers nothing, and an entry whose `sourceExtension` the bound
  delegate does not advertise is dropped (so HdEmbree has no sampler subtypes,
  no `curve`, and no `channel.*` it lacks; the materials' `ANARI_SAMPLER`-typed
  inputs stay listed there because KHR_MATERIAL_* sources them). Defaults, descriptions,
  `required`, `elementType`, `value` lists and `sourceExtension` (string and
  device-indexed INT32) answer straight from the tables. Queries are coarse
  feature detection (and the debug layer's app-side checks), not a per-parameter
  capability report, so **the device honours less than the schema states** in
  these known places, documented here instead of encoded into the tables:
  - Cameras: `near`/`far` are not read; `hydra.clippingRange` replaces them.
    `aspect` has no schema default (the device fits the frame).
  - Geometry: `vertex.tangent`/`faceVarying.tangent` are not read; the schema's
    64-bit `primitive.index` element types are consumed by `sphere` only, the
    other geometries read 32-bit indices. Surface `visible` is not read.
  - Materials: matte/physicallyBased inputs the Hydra network cannot map are
    warned and dropped (`sheenColor`, `sheenRoughness`, `specular`,
    `specularColor`, `transmission`, `thickness`, `attenuationColor`,
    `attenuationDistance`, `iridescence*`, `clearcoatNormal`); `normal`
    accepts a sampler only. On HdEmbree (no `material` sprim) materials
    degrade to the displayColor fallback: sampler- and attribute-typed inputs
    and the physicallyBased inputs beyond `baseColor`/`opacity`/`alphaMode`
    are ignored even though listed.
  - Lights: directional `angularDiameter`, point `radiance`, quad `side` and
    `intensityDistribution`, ring `innerRadius` (warned), `c0`,
    `openingAngle`, `falloffAngle` and `intensityDistribution`, hdri `up`,
    `direction` and `layout` are not applied. The device's own defaults differ
    from the schema for spot `falloffAngle` (0, not 0.1) and ring `radius`
    (1, not 0).
  - Volumes/fields: `filter: nearest`/`cubic` render linear
    ("Volume costs and limitations" below).
  - Instance/world/group: instance `bounds` is not exposed ("Scene bounds
    properties" below). The `transform` array also accepts `FLOAT32_MAT3x4`
    elements, which the merged `elementType` list (KHR's `{MAT4}`) cannot
    show.
  - Renderer: `background` is listed as both `FLOAT32_VEC4` and `ARRAY2D`;
    the `ARRAY2D` form reports `KHR_RENDERER_BACKGROUND_IMAGE` but shares the colour form's description, which the
    generator keys by parameter name.
  - Frame: `channel.color`/`channel.depth` are core schema entries with no
    source extension, so they are always listed even though the device claims
    `KHR_FRAME_CHANNEL_DEPTH` per delegate (both reference delegates claim it).
  - Samplers: `image3D` (pulled in by `KHR_SAMPLER_IMAGExD_CLAMP_TO_BORDER`'s
    schema dependency) is not a subtype and answers nothing.
  Asserted in `query_object_info` (storm + embree): every advertised subtype
  answers with `required` for every entry, KHR parameters are present iff their
  extension is claimed, the bridge parameters are present, the frame channel
  list equals the claimed `KHR_FRAME_CHANNEL_*` set, and the renderer's list
  keeps its dynamic delegate settings. `KHR_RENDERER_BACKGROUND_COLOR` and
  `KHR_RENDERER_BACKGROUND_IMAGE` are claimed on every delegate (the frame's
  clear colour / readback composite); `KHR_RENDERER_AMBIENT_LIGHT` is claimed
  with `KHR_LIGHT_HDRI`, wherever the delegate has dome lights.
- **Per-delegate extension truth** — the device probes the bound
  delegate's published capability lists (`GetSupported{R,S,B}primTypes`) once on
  the worker thread and gates its reported ANARI extension and object-subtype
  lists on them, honestly omitting what the delegate cannot do. Storm publishes
  `basisCurves`/a `material` sprim (so `GEOMETRY_CURVE` and
  the `SAMPLER_IMAGE*` extensions and their subtypes stay); HdEmbree publishes
  neither (so they drop) but keeps mesh geometry, sphere/cone/cylinder, all six
  lights, and the materials (which degrade to `displayColor`). Both the
  library `getDeviceExtensions` and the device `extension` property report the
  gated list (asserted in `query_extensions` for storm and embree).
- **Frame path** — per-frame render index + delegate + task controller +
  AOV color buffer; usdrecord-style Execute/converge loop; `map` reads the
  buffer back on the worker. Selection tracker set to suppress selection-highlight noise.
- **Frame completion callbacks** — `KHR_FRAME_COMPLETION_CALLBACK` on
  every delegate. Each render job ends by marking the frame ready and invoking
  the committed `frameCompletionCallback` with its user data on the worker
  thread, once per `anariRenderFrame`, including renders that fail (no
  delegate) and discarded ones (discard does not cancel in v1). Every API
  call is legal inside the callback, as the spec requires;
  `anariFrameReady(WAIT)` there, which the spec forbids, reports the current
  state instead of deadlocking. Calls on the completing frame skip helium's
  per-object lock (helium's `invokeCompletionCallback`), so an app thread
  blocked on the same frame cannot deadlock them. Calls on another frame see
  its last completed render and must not overlap an app call on that frame.
  Device release
  drains queued renders first, so their callbacks fire before it returns, and
  drops re-renders requested during release. `frame_callback` (storm + embree)
  covers counts and user data over repeated/queued renders, replacement and
  removal, in-callback ready/duration/color+depth map/retain/release/discard
  while the app is blocked in WAIT or in a map of the same frame, setting and
  committing the frame's own size and user data from its callback, querying,
  mapping, and re-rendering another frame from a callback, a re-render
  chain, discard, frame and device release with a pending render, device
  release under an unbounded chain, two app threads, and the failed render
  path.
- **Standard AOV channels** — depth, normal, and the
  object/primitive/instance IDs implement the existing `KHR_FRAME_CHANNEL_*`
  extensions per delegate. A `channel.<name>` frame parameter enables the channel;
  the frame wires the extra Hydra AOV bprims via `SetRenderOutputs` (guarded to
  the enabled-set change, mirroring the resize guard) and `map()` reads each back
  by `HdFormat`: `depth`→`FLOAT32` Euclidean camera distance,
  `normal`→`FLOAT32_VEC3` world-space shading normal, and IDs translate to
  `surface.id`, `instance.id`, and `primitive.id` (or the original primitive
  index). Unset surface/instance
  IDs and misses are `UINT32_MAX`. Lookup data is snapshotted with each rendered
  frame. Generated cone/cylinder faces retain original primitive indices.
  Storm uses the existing full-viewport Hdx picking task because its color task
  omits `elementId`; requesting standard `primitiveId` adds a picking render and
  readback. With primitive IDs enabled, all three standard ID channels use that
  same picking pass for consistent identities. Its sample coverage can differ
  from native AOVs at silhouettes; native AOV readback is unchanged.
  HdEmbree reports triangle indices for unrefined meshes; a separate
  lookup maps these back to original primitives. Other delegates use coarse
  face indices.
  The extensions gate on the delegate's
  `GetDefaultAovDescriptor(token)` at probe time, so the reported list is
  honest per delegate; both reference delegates publish all five. The normal
  channel resolves through one shared `normalAovToken(delegate)`: the
  world-space `normal` AOV (embree) or the eye-space `Neye` AOV (storm has no
  `normal` descriptor). The probe is descriptor-based; that the bound pass
  actually fills the target is proven by `render_normal_channel`, not at probe
  time. On storm the frame binds `Neye` in
  place of `normal`, pins it to `Float32Vec4` (Storm writes the raw eye-space normal as
  `vec4(Neye, 1)`, which its default `UNorm8Vec4` target would clamp and
  quantize), and `readbackNormalFromNeye` rotates each hit through the
  transpose of the rendered view matrix and renormalizes. Misses read `(0,0,0)`
  on both delegates (the clear value is overridden; embree's native clear is
  -1). The value is the delegate's shading normal: the app's `vertex.normal` /
  `faceVarying.normal` when authored, otherwise the delegate's own geometric
  normal — the device never generates normals. Two delegate-owned behaviors
  leak through as documented limitations: (1) geometry without authored normals
  reads whatever the delegate shades with (Storm's flat geometric normal here),
  not an app value; (2) Storm flips `Neye` toward the viewer on back-facing
  fragments (`mesh.glslfx` `gl_FrontFacing ? Neye : -Neye`), so a surface seen
  from behind reports the negated authored normal on storm while embree reports
  the authored direction — the channel's sign on back faces is delegate-defined.
  A concurrent `channel.aov:Neye`
  request shares the pinned `Float32Vec4` buffer, so its arbitrary readback
  only matches a `FLOAT32_VEC4` request. Asserted in `render_normal_channel`
  (storm and embree): a front-facing quad reads `(0,0,1)`, an authored tilted
  normal `(-0.6,0,0.8)` reads back exactly (negative component intact), a
  camera moved to +x and, separately, an instance rotated +90° about y both
  yield the world direction `(1,0,0)`, a quad without authored normals reports
  the geometric normal, a back-facing view reports a unit ±z normal (sign
  printed, delegate-owned), the background corner is `(0,0,0)`, and the
  delegate advertises `KHR_FRAME_CHANNEL_NORMAL`. Asserted in
  `render_aov` (storm and embree): two quads at different depths — color still
  covers, numerical center/off-axis ray distances and positive-infinity misses
  survive a rectangular resize, and the two surfaces carry distinct object IDs
  (each ≠ background). `render_picking` checks original IDs across direct and
  repeated-group geometry, ID edits and defaults, generated tube faces, indexed
  and unindexed curves, frame world replacement, and hidden object-ID dependencies.
  Native named AOVs retain delegate IDs. Standard depth unprojects native [0,1] raster depth
  using the rendered projection and pixel centers; native clear depth 1 becomes
  positive infinity. This follows the [ANARI depth contract](https://registry.khronos.org/ANARI/specs/1.1/ANARI-1.1.html#frame)
  for perspective cameras; orthographic depth is distance along parallel rays
  from their origins on the camera plane. Precision remains limited by
  the delegate's depth buffer and clip range; subpixel sample positions are not
  exposed by Hydra, so distances use pixel centers. `channel.aov:depth` remains
  native normalized depth (including clear value 1), tested simultaneously with
  standard depth. No extension/query changes are needed: this corrects the
  existing `KHR_FRAME_CHANNEL_DEPTH` implementation.
  The render loop dispatches one Execute then polls
  convergence with a short sleep under a wall-clock budget  rather
  than a fixed spin count — a progressive delegate (embree) renders on a
  background thread, so a fixed count can exhaust before it finishes and read
  back an empty frame.
- **Arbitrary named AOVs** — `ANARI_HYDRA_FRAME_AOV` exposes any Hydra
  AOV as a frame channel parameter `channel.aov:<hydraToken>` through the
  standard `anariMapFrame` path. `Frame::commitParameters` enumerates the frame's
  params by the `channel.aov:` prefix (helium `params_begin()/params_end()`),
  recording each `(hydraToken → requested ANARIDataType)`; `renderOnWorker` adds
  those tokens to the single `SetRenderOutputs` call (guard key extended to the
  full standard-∪-arbitrary set) and honors each requested format on its bprim
  via `SetRenderOutputSettings` (ANARI→HdFormat: FLOAT32→Float32,
  FLOAT32_VEC3/4→Float32Vec3/4, UINT32→Int32, UFIXED8_VEC4→UNorm8Vec4);
  `map("channel.aov:<token>")` resolves the buffer and converts to the requested
  type (nullptr for an unrequested token). The extension gates per delegate on
  the delegate publishing the `renderBuffer` bprim — the color channel
  already proves the AOV path, so the claim is honest wherever a render buffer
  exists (storm and embree both). Asserted in `render_aov_named` (embree):
  requesting the arbitrary `channel.aov:cameraDepth` (view-space linear depth,
  not a standard ANARI channel) over two quads yields a smaller near-quad depth
  than the far quad, color still covers, and an unrequested arbitrary AOV maps to
  nothing.

- **Volumes** — `structuredRegular` (and `nanovdb`, below) spatial
  fields classified by `transferFunction1D` volumes, on delegates publishing
  the `volume` rprim, the `openvdbAsset` field bprim, and the `material`
  sprim (Storm; HdEmbree has none, so it claims nothing and lists no volume
  or field subtypes). No OpenVDB grids are built, so the device does not need an OpenUSD
  built with OpenVDB support.
  - *Fields.* Every field subtype decodes at commit to one dense float32
    vertex-centred grid (`scene/AnariField.h`), registered under a
    revision-bumped `anari://field/f{F}_r{R}.anari` path. A second Hio plugin
    in the device library (`AnariHioFieldTextureData`, `fieldDataTypes:
    ["anari"]` in `plugInfo.json`) serves that grid to Storm's stock
    `openvdbAsset` field bprim, which dispatches its loader on the asset
    extension only. The served box is the index range widened by half a
    voxel with an index-to-object matrix, so Storm's texel centres land on
    the ANARI vertices and its step is half the smallest spacing (VisRTX's
    step). Element types UFIXED8/FIXED8/UFIXED16/FIXED16 read normalized,
    FLOAT32/FLOAT64 as float. A negative spacing is reordered at decode to
    the same domain with positive spacing. Any `filter` other than `linear`
    renders linear (warned once per field): Storm fixes the field sampler.
    Each decode is an immutable snapshot whose registry entry lives as long
    as the snapshot, and the mirror holds the one it authored, so a commit
    flushed on an app thread mid-render cannot unregister an asset Storm is
    about to load; volumes likewise publish their classification whole.
  - *Classification.* Every volume binds one fixed glslfx volume shader
    (`scene/VolumeBuilder.cpp`, registered with Sdr from source once per
    process) and a second field, `tf`: the volume's classification table
    (color rgba and opacity rows, plus `valueRange`, `unitDistance` and the
    field domain), served by the same Hio plugin as a W×6×1 float texture
    and read with exact `texelFetch`. `HwFieldReader_float` nodes make Storm
    bind the field as `HdGet_value` and the table as a field texture. Color
    and opacity are linearly interpolated over `valueRange`, and final
    opacity = color.a × opacity. Opacity becomes extinction
    `-ln(1 - α) / unitDistance` per object-space unit, as in VisRTX. The shader
    clips each of Storm's ray-march steps to the domain in object space
    along the actual ray and composites it exactly (emission =
    color × (1 − e^−depth) / step), so a constant classification renders to
    float precision for any step, instance scale or rotation, and camera
    projection; Storm's proxy box (half a voxel past the domain) adds no
    coverage. Storm merges every field a volume binds into one ray-marched
    box and steps at the smallest field spacing, so the table is served
    covering exactly the value field's proxy box with a matrix as long as
    that box (`AnariFieldData::table`): neither the box nor the step changes.
  - *Mirror.* Field bprims `/anari/w{N}/fields/f{F}` (one per field, shared
    by its volumes), classification tables `/anari/w{N}/volumeTables/v{V}`,
    one material `/anari/w{N}/volumeMaterial` shared by every volume,
    world-direct volumes `/anari/w{N}/volumes/v{V}`, and group volumes as
    prototypes `/anari/w{N}/instancers/g{G}/v{V}` of the group's instancer.
    Any volume or field revision (parameter commit or observed-array
    unmap) re-authors the field or the table under a new asset path, and
    the rprim: Storm does not refresh field textures from a bprim change
    alone. The material is never re-authored. Storm refreshes a volume
    material's parameters only when its source changes, which is why the
    classification is a texture and not a material parameter.
  - *Bounds.* Volumes add their field domain to group/world `bounds`
    (instance-transformed like surfaces). Camera clipping fits the proxy
    boxes instead, or the proxy's back faces clip away.
  - Asserted in `render_volume` (storm; embree asserts the absent claims):
    closed-form constant classification, `unitDistance`/opacity edits,
    `valueRange` ramps and clamping, in-place color-array edits, opacity
    arrays × color alpha, analytic linear (within 4e-4) and nonlinear fields
    column by column, an anisotropic non-cubic grid, negative spacing, a
    spacing edit, origin/spacing coverage and thickness, origin edits, in-place
    field-data edits, two volumes, two volumes sharing a field, scaled,
    rotated and multiply-instanced groups, a perspective camera, UFIXED8
    decode, `visible`, opaque surfaces inside and behind a volume, world
    and group bounds, and 16 distinct classification edits leaving the
    `hydra.shaderNodeCount` device property (the process's Sdr registry
    size) unchanged.
- **nanovdb fields** — built when `ANARI_HYDRA_ENABLE_NANOVDB` is ON (the
  default; headers downloaded or taken from `ANARI_HYDRA_NANOVDB_INCLUDE_DIR`,
  see "Build & test"); otherwise neither the subtype nor
  `KHR_SPATIAL_FIELD_NANOVDB` is offered.
  Scalar Float/Fp4/Fp8/Fp16/FpN grids with an axis-aligned index-to-world map
  are densified over the active index box plus one voxel layer on its max
  side, so the domain is the grid's `worldBBox` (VisRTX's bounds) and every
  value, including tiles and background, is the NanoVDB accessor's: sampling
  is exact to NanoVDB's own trilinear sampler. Asserted in `render_volume`
  (Float and Fp16 grids render identically to the equivalent
  structuredRegular field, bounds equal the `worldBBox`, and a column matches
  the analytic field).
- **Isosurfaces (KHR_GEOMETRY_ISOSURFACE)** — an `isosurface` geometry
  (`geometry/Isosurface.cpp`) extracts the level sets of its `field` at every
  `isovalue` (FLOAT32 scalar or FLOAT32 Array1D) into one triangle mesh at
  finalize, from the field's published `FieldSnapshot` grid — the same dense
  float32 representation volumes use — and authors it through the ordinary
  tessellated-mesh path (materials, attributes, instancing, picking, bounds).
  Claimed, and listed as a geometry subtype, wherever spatial fields are
  offered (Storm): isosurfaces need a field, and HdEmbree offers none. The
  mesh itself renders on any mesh delegate.
  - *Extraction.* Marching tetrahedra over the six Kuhn tetrahedra of every
    cell (all sharing the cell's main diagonal, so neighbouring cells split
    their faces alike and the surface is crack-free). Each crossing edge
    yields one shared vertex per isovalue, linearly interpolated; faces wind
    so their geometric normal points toward higher field values. The mesh is
    the exact level set of the field's piecewise-linear (tetrahedral)
    interpolant, which is exact for fields linear in x, y, z; it is not the
    trilinear level set the volume shader samples. Cells with a non-finite
    sample produce nothing; a non-finite isovalue is skipped.
  - *Primitives.* The ANARI primitive is the isovalue (as VisRTX, which
    reports the isovalue index as primID): every face carries its isovalue's
    index, so `primitive.color`/`attribute0..3` are indexed by isovalue
    (uniform primvars; a too-short array is warned and dropped; a short
    `primitive.id` is warned and reads "no ID" past its end),
    `channel.primitiveId` reads the isovalue index, and `primitive.id` maps it
    to a user ID. The constant `color`/`attribute0..3` apply as on any
    geometry. Vertex, face-varying and index parameters do not apply and are
    ignored.
  - *Shading normals.* None are generated: no app-provided normals exist for
    an extracted surface, and the device never synthesizes normals, so Storm
    flat-shades each triangle from its winding (doubleSided). VisRTX shades
    with the field gradient instead, so its isosurfaces look smooth where
    these look faceted at the grid resolution.
  - *Invalidation.* The geometry observes its field and isovalue array: a
    field commit (data, origin, spacing), an in-place field-data or isovalue
    unmap, or rebinding either re-extracts at the next finalize, and the
    mirror re-authors the rprim (tessellated meshes always rebuild).
  - *Empty surfaces.* With no crossing, no field, or no isovalue the
    geometry is invalid for rendering: no rprim is authored and it adds
    nothing to group/world `bounds`; a later edit that crosses brings it back.
  - Asserted in `render_isosurface` (storm; embree asserts the absent claim
    and still renders the mesh): an r = 1 sphere of f = |p| (coverage, area,
    exact axis bounds), constant `color`, scalar-isovalue, in-place field-data, field-origin,
    and field-object edits, an empty isovalue and its recovery, a scaled and
    translated instance (world vs group bounds), two tilted planes of a
    linear field (exact bounds, per-isovalue `primitive.color`,
    `primitiveId` 0/1 and `primitive.id` 7/9, per-isovalue
    `primitive.attribute0` and constant `attribute0` through the material,
    in-place isovalue-array edit),
    and the measured approximation error below.

## Deferred / blocked
- **The self-re-posting converge poll.** The render loop now correctly
  waits for the delegate to converge (one dispatch + sleep-poll under a wall
  budget), which blocks the worker for the render duration — fine for v1's
  strict per-device render serialization. The *non-starving*
  variant (re-enqueue the poll so waiters/teardown interleave) needs helium's
  worker-side `TaskQueue` enqueue change (outside this device), so it stays
  deferred.
- **Mixed-group override fill.** An instance-rate primvar array is
  all-or-nothing across a group, so in a group with **no** per-vertex primvar of
  that name where only *some* instances set the attribute, the unset instances
  render the ANARI default (0,0,0,1) rather than the material reader's own
  fallback. Where the prototype carries its own per-vertex primvar, that value
  wins for every instance (verified), so this gap is confined to the partial,
  no-per-vertex case.
- **`normal` as a constant/attribute.**
  The `normal` slot accepts any sampler (the normal-map case, implemented
  above; transform/primitive samplers through a generated Storm shader); a
  constant or attribute `normal` has no faithful UsdPreviewSurface target and
  is warn-and-dropped (the surface shades from its geometric normal).
  `clearcoatNormal` stays warn-and-drop: UsdPreviewSurface has a single
  shading normal, so a separate clearcoat normal would need its own lighting
  changes, not just a swapped normal-mapping section. The color + scalar slots
  (baseColor/color, opacity, metallic, roughness, emissive, occlusion, ior,
  clearcoat, clearcoatRoughness) accept constant/attribute/sampler. Reflection
  covers the common VtValue types; exotic holdings (GfVec3d, int-vecs,
  SdfAssetPath, arrays) warn-and-drop.
- **Smaller items:** locator-precise light dirtying (light sync
  re-authors the full set rather than dirtying only the changed transform
  locator), the device-wide worker-anchor registry for non-frame objects,
  and two helium changes outside this device.

### Indexed sphere geometry

Spheres use closed, inscribed meshes with 32 longitude segments and 16 latitude
intervals (482 vertices, 960 triangles per sphere), with radial smooth normals.
The conservative object-space radial error is at most `1-cos(pi/16)` times the
radius (1.93%). Under an affine instance transform the absolute surface error is
bounded by that value times the transform's largest singular value. Silhouettes
and depth remain tessellated approximations; shading normals do not change depth.

`primitive.index` (UINT32 or UINT64) selects/reorders centers, radii, vertex color,
and vertex attributes; generated faces retain the original primitive ID. Array
unmaps regenerate meshes during finalization, including positions, radii and
indices; copied attributes are rebuilt in the mirror. Missing per-vertex radii
use the global radius. Nonpositive/nonfinite radii and out-of-range indices are
skipped. Attributes shorter than the selected vertex range are omitted. Both
Storm and Embree support these meshes; Embree still uses constant displayColor
instead of attribute-bound material networks. Primitive/constant geometry
attributes are supported as described below.

`render_spheres` checks overlapping indexed spheres, an intersecting plane,
nonuniform instances, and in-place index/radius/position/attribute edits against
analytic ray/ellipsoid intersections in both delegates. Geometry and attribute
storage scale linearly with 482 vertices and 960 faces per sphere; meshes are
rebuilt on edits rather than instancing a shared unit sphere internally.

### Geometry attribute interpolation

All supported geometry subtypes accept `primitive.color` and
`primitive.attribute0..3` arrays, plus `color` and `attribute0..3`
constants of type FLOAT32_VEC4. Native mesh face-varying data wins over vertex
data; vertex data wins over primitive data; instance values then override
geometry constants. Unspecified geometry attributes stay absent so inherited
instance primvars can supply them.

Primitive arrays use Hydra uniform interpolation and original ANARI primitive
indices, never application `primitive.id` labels. Tessellated spheres and tubes
replicate primitive values across generated faces. Tube ring vertices and cap
centers inherit the corresponding endpoint value, so side interpolation follows
the segment axis. Native mesh position indices do not reorder face-varying or
primitive arrays. Observed primitive/vertex array edits and parameter removal
rebuild copied primvars on the next render.

Each attribute also has typed scalar and float3 primvars for material readers.
This avoids Storm shader type mismatches when one FLOAT32_VEC4 attribute feeds
both color and scalar inputs, including when geometry is shared by different
materials. Texture `st` uses the first two components of resolved attribute0.

**Per-instance surface copies .** A surface whose material
reads `worldPosition`/`worldNormal` (`ReadsWorldSpace`) is not a prototype in
an instanced group: `Mirror::sync` authors one plain rprim per instance element
at `/anari/w{N}/surfaces/i{I}[_t{k}]_s{S}`, with the instance transform as its
`xform` (so `PrimBuilder`'s world variants are exact) and the instance's
overrides as constants (`InstanceBinding::values`). Its picking entry is marked
`perInstanceCopy`, so the instance channel reports the instance `id` for every
hit. Cost: one rprim and one copy of the baked data per instance, for these
surfaces only — linear and uncapped, like the light expansion.
`render_instance_override` covers world x on both instances and prototypes,
instance IDs, and overrides on the copies.

When geometry constants coexist with instance overrides, only affected surfaces
get a separate instancer. Its instance arrays fill unset entries from that
surface's geometry constants, while higher-rate geometry data remains on the
prototype. Ordinary groups retain their shared instancer. The tradeoff is one
instancer and copied override arrays per affected surface.

Geometry parameter-list queries report these inputs per subtype; no new ANARI
extension is needed. Embree still uses material display-color fallback and does
not consume these attribute material networks. Face-varying attributes apply to
native triangle/quad meshes, not generated sphere/tube topology. Attribute array
component decoding retains the existing ANARI normalized/sRGB conversion.

`render_attribute` covers all five channels, indexed precedence, live vertex
and primitive edits, removal, generated tubes, and shared scalar/color material
bindings. `render_instance_override` covers distinct prototype constants,
unset instance entries, edits, and transitions between shared and separate
instancers.


### Camera projection

Perspective and orthographic cameras share position/direction/up, optional
`aspect`, and `imageRegion` (`FLOAT32_BOX2`, default `[0,0,1,1]`). Explicit
aspect defines the full image width/height; when absent, the current frame's
width/height is used. A region crops that full image window and maps it to the
entire output frame. Set the full-image aspect explicitly when rendering tiles
whose aspect differs from the full image. Hydra window conformity is disabled
so these projections also govern depth reconstruction and picking.

Perspective `fovy` defaults to 60 degrees (radians at the API); orthographic
`height` defaults to 1 world unit. Camera commits are consumed on the next frame.
Both fit clipping distances to committed world bounds in camera space each render,
including geometry/instance edits between frames. Positive nearest/farthest bounds
receive 1% outward padding. Bounds crossing the eye use near = far × 1e-4;
empty or entirely behind-camera worlds use 0.01–1000. Float depth precision still
limits scenes spanning extreme simultaneous distance ratios; eye-crossing scenes
can lose content closer than that relative near distance. Camera parameter
`hydra.clippingRange` (FLOAT32_VEC2) overrides near/far with finite 0 < near < far;
(0,0), unset, or invalid values select automatic fitting (invalid values warn).
Camera parameters are reflected with the KHR schema's defaults/descriptions
through core queries (object introspection; `near`/`far` are listed but not read);
subtype and extension queries include orthographic support.

### Scene bounds properties

Groups and worlds expose the core `bounds` property as `ANARI_FLOAT32_BOX3`
(lower XYZ then upper XYZ). `ANARI_WAIT` serializes the commit drain and bounds
observation on the device worker, without rendering or authoring the mirror.
`ANARI_NO_WAIT` returns the last completed query snapshot (initially empty),
so pending edits may not be reflected. Empty bounds have lower > upper on each
axis. Wrong names/types, null output, and buffers smaller than six floats fail
without writing. Bounds are core properties, so no extension token is added; object introspection
reflects parameters only (ANARI defines no property info), so nothing claims
`bounds` anywhere.

Instances do not expose `bounds`, even though `KHR_INSTANCE_TRANSFORM` declares
the property: bounds are reported only on groups and worlds. This is a known,
accepted deviation (VisRTX omits instance bounds as well) and must not be
"fixed" by dropping the extension from the advertised list.

`Geometry::bounds()`, `Group::bounds()`, and `World::bounds()` are committed-state
helpers for worker-side consumers such as camera clipping. Queries derive extents
from current geometry each time: referenced triangle/quad vertices, generated
sphere/cone/cylinder and isosurface meshes, and conservative radius-expanded linear curve
endpoints. Group boxes are transformed at all eight corners for each instance
and unioned with world-direct surfaces. These boxes can be conservative after
rotation. Lights do not contribute; volumes contribute their field domain
(camera clipping uses their slightly larger ray-march proxy box instead). Array edits
refresh tube tessellation during finalization, including radius/index/position
changes, without requiring a geometry recommit.

### Volume costs and limitations

- **Memory.** A field holds a float32 copy (4 bytes per voxel) in the
  registry for its lifetime, and Storm uploads a float32 3D texture of the
  same size per field (shared by every volume and world using it). Storm's
  per-field texture-memory budget is not applied: grids upload at full
  resolution. A nanovdb field densifies its whole active box (+1 layer), so
  a sparse grid costs its dense bounding box, not its active voxels; the
  aligned staging copy of the blob is freed after decoding. Every volume or
  field edit decodes again and uploads a new texture.
- **Shader cost.** One Sdr node and one Storm program serve every volume.
  A classification edit (transfer function, valueRange, unitDistance, field
  domain) builds and uploads a new classification table of
  6 × max(table length, 11) floats and compiles nothing. Before this design
  the classification was baked into shader source: each edit added an Sdr
  node, which is never freed, plus about 1 MB of resident memory and a
  ~450 ms shader compile. Tables longer than 1024 entries are
  resampled linearly to 1024 (error at most the table's own interpolation
  error over one resampled interval).
- **Accuracy.** Constant classification is exact per step. A varying field
  carries the midpoint rule's O(step²) error per step (step = half the
  smallest spacing) on top of the trilinear reconstruction ANARI prescribes;
  measured against a numerically integrated analytic field (quadratic along
  the ray, sinusoidal across it, 33³ grid) the worst column error is 1.5e-3
  in color (`render_volume`). Where an opaque surface ends a ray inside a volume, Storm stops at
  `floor(length / step)` steps, dropping up to 1.5 steps of volume in front
  of the surface (asserted to 0.03 in `render_volume`). Storm's early exit at
  transmittance 0.002 drops at most 0.2% of the light.
- **Emission-absorption only.** Volumes are unlit (no scattering), so scene
  lights and the ambient fill do not shade them; VisRTX shades volume
  albedo. Classification is post-interpolation like VisRTX, but VisRTX
  bins the transfer function into a 256-texel texture; the bridge evaluates
  the arrays directly.
- **Overlapping volumes** composite as whole volumes, one over the other in
  Storm's draw order, rather than interleaving samples: total opacity is
  right, the color split is not (asserted only as totals).
- **ID and depth channels** do not see volumes: they take no picking IDs
  (`channel.objectId` etc. read "no hit" through them) because Storm's volume
  pass produces no usable ID output. The volume `id` parameter is therefore
  not read (and not reflected: KHR_FRAME_CHANNEL_OBJECT_ID's volume entry is
  anonymous).
- **No OpenVDB (future work).** The dense float32 `FieldSnapshot` is the
  shared field representation for volumes (and isosurfaces), served to
  Storm by the in-library Hio plugin. The route originally planned — a USD
  built with `PXR_ENABLE_OPENVDB_SUPPORT=ON`, fields published as OpenVDB
  grids through an Ar resolver and `HioOpenVDBArAssetInterface`
  — should eventually be implemented: it keeps sparse grids
  sparse, applies Storm's texture-memory budget, and works with render
  delegates beyond Storm (hdPrman).
- **Unfinished field families.** VisRTX's `structuredRectilinear`
  (`VISRTX_SPATIAL_FIELD_STRUCTURED_RECTILINEAR`) and `nanovdbRectilinear`
  are vendor extensions, not KHR, and are not implemented; the dense-grid
  seam would take them by resampling onto a regular grid at the finest axis
  spacing (memory grows with the finest/coarsest spacing ratio; error is the
  resampling interpolation). `structuredRegular`'s VisRTX-only
  `dataCentering`/`roi` are not read. The cubic-filter and `unstructured`
  field extensions and `image3D` samplers remain out.

### Isosurface costs and limitations

- **Accuracy.** Linear interpolation of f along an edge of length l errs by
  at most l²·max|f''|/8; the longest Kuhn edge is the cell diagonal √3·h, so
  for a sphere level set of f = |p| the surface lies within 3h²/(8r) of the
  true sphere. Measured on the rendered surface (`render_isosurface`: object-
  space z read back through an emissive transform sampler under an
  orthographic camera, r = 1, 3-unit domain): worst radial error 0.0121
  (0.345 h²) at n = 17, 0.0033 (0.376 h²) at n = 33, 0.00079 (0.361 h²) at
  n = 65 — second order, at the predicted constant. Fields linear in
  x, y, z are reproduced exactly (the plane tests' bounds are exact).
  Silhouettes and depth are those of the flat triangles; shading is faceted
  (no normals, above).
- **Size.** A level set of area A in a grid of spacing h yields about
  9A/h² triangles (for example 201,600 triangles and 100,802 vertices for
  an r = 1 sphere in a 128³ grid over 3 units). The geometry holds 12 bytes
  per vertex and 20 per triangle (three indices, face count, isovalue index)
  for its lifetime, about 5 MB in that example; the mirror shares the point
  and topology arrays, and Storm uploads its own GPU copy. During
  extraction the peak adds the edge-to-vertex hash map (one entry per
  vertex of the current isovalue, roughly 30-40 bytes each) and the
  pre-compaction point array. Copied primvars
  (attributes, derived sampler values) add their own per-vertex or per-face
  arrays.
- **Time.** Extraction runs in stage 1 on the device worker, single-threaded,
  visiting every cell once per isovalue: O(cells × isovalues). A 128³ field
  with one sphere level set takes about 50 ms (the device reports triangle
  count, vertex count and time at DEBUG severity). Any edit re-extracts the
  whole field, and every mirror sync rebuilds the rprim, as for all
  tessellated geometry.
- **Field support.** Whatever the field publishes as a dense grid:
  `structuredRegular`, and densified `nanovdb` (so a sparse VDB costs its
  dense bounding box here too). Future OpenVDB-backed fields (Volume costs
  and limitations) would need a sparse extraction path.
- **Not implemented.** Gradient (smooth) shading normals, which would need
  device-generated normals; adaptive or dual-contouring extraction; the
  trilinear level set (the volume shader's reconstruction) rather than the
  tetrahedral one. HdEmbree does not claim the extension because it offers no
  field subtypes, although it renders the extracted mesh.

## OpenUSD upgrade checklist

Places that rely on OpenUSD internals rather than public API. Re-check them
when moving to a new OpenUSD release.

- **Volume shader texture accessors** (`scene/VolumeBuilder.cpp`). The
  volume shader calls `HdGetSampler_tf_texture()` and tests
  `HD_HAS_tf_texture`. Both names are generated by Storm's codegen
  (`hdSt/codeGen.cpp`) from the field binding name plus
  `HdSt_ResourceBindingSuffixTokens->texture`. If they change, the shader
  falls back to an all-zero table and volumes render empty, which
  `render_volume` catches. The design also relies on Storm merging field
  boxes with `GfBBox3d::Combine` and stepping at the shortest field-matrix
  column (`hdSt/volumeShader.cpp`); see `AnariFieldData::table`.
- **Generated normal-mapping surface** (`scene/NormalMappedSurface.h`). Its
  glslfx copies the parameter list and defaults of Sdr's `UsdPreviewSurface`
  node and imports `previewSurface.glslfx`, naming its internal sections
  (`Preview.LightStructures`, `Preview.LightIntegration`, `Preview.Surface`,
  `SurfaceHelpers.TangentSpace`, ...) and replacing `Preview.NormalMapping`.
  An upgrade that adds, renames or re-defaults an input, or changes the
  section list, can leave the node registering but silently missing the input
  or failing to compile, in which case Storm draws the prim with its fallback
  shader and only emits a Tf warning. `guard_normal_map_shader` catches this:
  it compares the parameter list (names, value types, defaults; `opacityMode`
  as Storm's int encoding) with Sdr's node and the technique with
  `previewSurface.glslfx`'s, then renders a primvar normal map on Storm and
  fails on any Tf diagnostic or on fallback shading. When it fails, update the
  header to match the new `UsdPreviewSurface` and `previewSurface.glslfx`.
  Unlike the other tests it is white-box: it links OpenUSD and includes the
  header.
