---
status: accepted
---
# The pathtracer samples halcyon samplers with the preview renderer's ISPC code

> **Tracker references.** This record predates halcyon's move into VisRTX: its ticket
> and follow-up numbers ("pathtracer ticket N", "follow-up N") refer to halcyon's
> original private tracker, and its commit hashes to the deprecated `anari-halcyon`
> repository (see AGENTS.md, Overview).

Typhoon's materials read textures through `mxcpp::TextureSystem::Sample2D()`, which its
image nodes call with a `file` key and texture coordinates `st`. Halcyon's textures are
ANARI samplers, whose sampling (filtering, wrap modes, the in and out transforms, every
subtype) the `preview` renderer already does in ISPC. The pathtracer must sample them
alike, so there is one implementation: the sampling code is in
`shading/halcyon_sampler.isph`, the preview renderer's hooks (`Sampler.ispc`) run it, and
the pathtracer's texture system (`renderer/pathtracer/HalcyonTextureSystem.*`) runs it
through single-lane ISPC entry points (`HalcyonTexture_sample()`). A sampler's ISPC object
records which code its hook runs (`Sampler::kind`), since the texture system can't compare
hook addresses from another library.

Each compiled material (`CppMaterial`) owns a `HalcyonTextureSystem` over the samplers its
graph names, by index; its shading contexts point at it. There are two ways in:

- **Sampler node** (`halcyon_sampler_vector4`, registered with MaterialXCpp's node registry
  through its public `Register()`, so the vendored code is untouched): the graphs
  halcyon builds (matte, physicallyBased, openPBR, usdPreviewSurface) and `materialx`
  overrides of shader inputs use it for every sampler-sourced parameter.
  Its `in` is the sampler's `inAttribute` through the same geomprop node an
  attribute-sourced parameter uses, so the integrator resolves that attribute exactly as
  the preview renderer does; the node passes it, and the hit's primitive (`faceId`, for
  primitive samplers), to the whole sampler. Image1D, image3D, transform and primitive
  samplers therefore need no extension of the 2D-only `TextureSystem`, no graph nodes for
  their transforms, and no values evaluated before the graph: everything stays lazy, in
  the graph, and bit-for-bit the preview renderer's.
- **`Sample2D()`** with a key `anari:sampler/<index>`: the sampler with the image node's
  `st` as its input attribute's value, (s, t, 0, 1). This is for MaterialX image nodes
  bound to ANARI samplers (pathtracer ticket 22; VisRTX binds only image2D samplers).
  A sampler that takes no input there (a primitive sampler, or `inAttribute` "none") is
  `Missing`.

## Decisions

- **Derivatives and filtering**: halcyon samplers have no mip levels, and the preview
  renderer samples them without a footprint, so the request's `dstdx`/`dstdy`, blur,
  address modes and filter type are ignored; the sampler's own wrap modes and filter
  decide. (As implemented with ticket 22: this holds for `anari:sampler/` keys; image
  files honour the request's address modes, filter and footprint, which picks their mip
  level, so the screen-space derivative fallback, AGENTS.md "Shading context", changes
  file-textured images.)
- **Colour**: sampler values are taken as they are: ANARI arrays have no colour space, and
  the preview renderer doesn't convert them either.
- **Texture origin**: ANARI's coordinates are the array's: coordinate 0 is the first texel,
  with no flip, as in the preview renderer. MaterialX's `st` has a lower-left origin, which
  only matters for image files (below). hdAnari flips v on the texcoord primvars it binds,
  which suits the top-row-first images it loads into ANARI arrays; pathtracer ticket 28
  checks USD scenes through it.
- **Invalid samplers**: a sampler node gives the parameter's value (the preview renderer
  falls back to it too), `Sample2D()` the request's default with `Missing`.
- **Freshness**: a sampler node reads the sampler's `inAttribute` when the graph is built, so
  a material observes its samplers (`ChangeObserverPtr`) and builds its graph again when one
  is finalized. Everything else (image, transforms, validity) is read from the sampler's ISPC
  object at sampling time.
- **Normal maps**: physicallyBased `normal` and `clearcoatNormal` hold tangent-space normals
  in [-1, 1] (the specification); the graph re-encodes them to [0, 1] for MaterialX's
  `ND_normalmap_float` at scale 1, on glTF PBR's `normal` and `clearcoat_normal`. Without
  its own map the clearcoat gets the unperturbed normal, as glTF specifies (the glTF node
  would otherwise give it the base layer's). `occlusion` isn't used: glTF PBR has no input
  for it, and the specification applies it to ambient light only.

## File-backed textures (design; pathtracer ticket 22 implemented it)

MaterialX documents (ticket 22) name image files. `Sample2D()` resolves keys other than
`anari:sampler/...` as file paths:

- **Loader**: stb_image (and stb_image's HDR path), as the ANARI SDK's tools use, fetched
  at configure time like OpenQMC. OpenImageIO, Typhoon's backend, would bring a large dependency
  tree (and Boost or fmt, OpenEXR, libtiff...) into every halcyon build for features (tiled
  `.tx`, UDIMs, EXR layers) no ticket needs yet; if one does, OIIO can sit behind the same
  key interface. 8-bit sRGB-tagged images are decoded into linear floats once. (As
  implemented: values are kept as stored and colour data is converted after filtering,
  per sample, as Typhoon's backend does; see Colour below.)
- **Cache**: one per device, shared by the materials' texture systems (each holds a shared
  pointer), keyed by the resolved path, loaded on first use under a mutex and read without
  one afterwards (as implemented: loaded when a material's graph is built, in
  `finalize()`, never during a render, and read again when the file changes; the cache
  holds weak pointers, and each material's texture system keeps the images its graph
  names in a map of its own, which renders read); images are float4 texel arrays with a
  box-filtered mip chain, so the
  request's derivatives pick a level (trilinear), as OIIO's do for Typhoon.
- **Sampling**: the same ISPC filtering code over the cached texels (as implemented: no
  sampler object; `HalcyonTexture_sampleImage()` runs `Image2D_filterAt()` on each mip
  level's texels), with the request's address modes mapped to halcyon's wrap modes and its
  filter to nearest (closest) or linear (the others); `Constant` and `UseMetadata` return
  the request's default outside [0, 1].
- **Origin**: files store the top row first while MaterialX's `st` starts at the bottom, so
  `t` is flipped (rows are flipped on load), as Typhoon's backend flips `t`.
- **Colour**: `sourceColorSpace` and `dataRole` convert colour data into the working space
  through `ColorManagement`'s `convertToWorkingSpace()`, as Typhoon's backend does with
  `_ApplyColorTransform()`.
- **Failure**: an unreadable file warns once per path and returns the request's default with
  `Error`.

## Considered options

- **Image nodes plus graph math for ANARI materials** (`ND_image_*` on a texcoord built from
  the attribute by matrix nodes): the in-transform mixes all four components of the input,
  which `Sample2D()`'s `st` can't carry; image3D needs a third coordinate and primitive
  samplers the primitive; and C++ matrix nodes round differently from ISPC's.
- **Evaluating every sampler in ISPC before the graph** and passing the values as geomprops:
  exact, but every sampler a material names costs a row in the per-lane record whether the
  graph reads it or not, and the ISPC material would need a list of its samplers.
- **Extending `TextureSystem` for 1D/3D** (a local patch to the vendored copy): not allowed
  by ADR 0002, and unnecessary given the sampler node.
- **A device-wide sampler registry with keys from object ids**: keys could then name any
  sampler, so a key forged in a MaterialX document could reach freed memory; per-material
  tables only name samplers the material keeps alive.

## Consequences

- `struct Sampler` has a `kind` (the slot stays 240 bytes).
- `halcyon_path_materials` compiles `HalcyonTextureSystem.ispc`, which includes the
  sampling code; the device compiles it again in `Sampler.ispc`. It is one source.
- A sampler commit finalizes the materials that use it again, which recompiles their graphs.
- MaterialX materials (ticket 22) get `Sample2D()` with ANARI samplers for free, and file
  textures (ticket 22) plug into the same keys.

Accepted 2026-10-04 by the maintainer, with the file-texture loader amended to stb_image plus
tinyexr (EXR) and a small ASCII PPM reader; OpenImageIO only if `.tx`, TIFF or UDIM become
necessary (ADR 0006, pathtracer ticket 22).
