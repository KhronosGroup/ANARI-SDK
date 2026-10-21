---
status: accepted
---
# The pathtracer's materials share one per-lane interface, dispatched by material kind

> **Tracker references.** This record predates halcyon's move into VisRTX: its ticket
> and follow-up numbers ("pathtracer ticket N", "follow-up N") refer to halcyon's
> original private tracker, and its commit hashes to the deprecated `anari-halcyon`
> repository (see AGENTS.md, Overview).

The `quality` renderer's path loop (ISPC, ported from Typhoon's integrator) needs the same
things from every material, whether ISPC shades it natively or Typhoon's C++ does through
the batched calls of ADR 0003. `renderer/pathtracer/PathMaterial.isph` is that interface:
varying functions the loop calls per gang, which switch on each lane's `Material::kind`.
Native kinds run inline; `MATERIAL_KIND_CPP` lanes go together through one batched call
per gang into `PathMaterial.cpp`, whose closures stay in the per-thread arena and are
named by per-lane handles. The SoA records and flags are declared once in the `.isph`; C++
reads them from the ispc-generated header.

| Call | Per | Does (Typhoon's code it stands for) |
|---|---|---|
| `PathMaterial_shade()` | gang and path vertex, once | evaluates the material, prepares normals, resolves presence, prunes caustic lobes, starts the hero wavelength, samples the BSDF (`_IntegratePath()` from graph evaluation to `SampleSurface()`); returns emission, flags, the shading normal, the sample, a handle |
| `PathMaterial_eval()` | light sample | f·\|cos\| (each lobe projected by its own normal) and the BSDF pdf towards `wi` (`_ComputeDirectLightingMIS()`) |
| `PathMaterial_subsurface()` | lanes with a subsurface sample | entry direction (`SampleSubsurfaceEntry()` if the sample had none) and the walk's coefficients (`_TraceSubsurface()`) |
| `PathMaterial_medium()` | lanes with an interior medium | the medium (`_UpdatePathMedium()`) |
| `PathMaterial_shadow()` | shadow-ray blocker | a visibility-only evaluation: opacity, presence, transmission, straight-through transmission, interior medium (`_Visibility()`) |

The shading call samples too, rather than a separate `sample(state, wo, u1, u2, uLobe)`
call: Typhoon samples exactly once per vertex, before it knows whether the path continues,
so a separate call would only add a crossing. Its random numbers (presence, hero
wavelength, BSDF sample) come in with the shading point, as do the hero wavelength the path
already has and whether to prune caustic lobes (a diffuse-like ancestor with caustics off).
Out come the facts the loop branches on: presence skipped, BSDF or not, volume-only
boundary, thin-walled, reflection-only (for the closure and for the pruned BSDF, which
Typhoon both uses), coupled thick dielectric, interior medium, subsurface, hero wavelength
started; and per sample specular, transmission, subsurface, diffuse-like and `eta`, which
are what caustic classification and ray differentials need. The state carries no opacity
or presence: Typhoon's path loop reads neither once presence is resolved, which the
shading call does (`PATH_MATERIAL_PRESENCE_SKIPPED`); shadow rays get both from the
shadow call.

## Arena lifetime

ADR 0003 left open whether a closure that lives until the gang's next shading call is
enough for subsurface scattering and interior media. It is: Typhoon reads the closure for
both at the same vertex, and keeps only copies (`SssInput` holds colour, radius, anisotropy
and precomputed coefficients; `MediumState` holds a copy of `MediumProperties`). So
`subsurface()` and `medium()` read by handle at the vertex and return plain data the loop
keeps. The shading call stores the sample's lobe normal and entry direction with the
closure for `subsurface()`. The shadow call keeps nothing, so it can run between the
shading call and the vertex's `eval()` calls, as NEE's visibility test does. At a walk's
exit on its own object, Typhoon replaces the material with a white Lambertian; the loop
flags the lane and it goes through C++ whatever its kind. An exit on another object gets
no closure in Typhoon, so the loop passes no material there.

## Normals

C++ lanes run Typhoon's pipeline unchanged: `ResolveGraphNormal()` in the exterior frame,
`ValidateLeafNormals()`, facing to the incident side by the geometric `frontFacing`
(never by the shading normal), then `Bsdf::detail::PrepareShadingNormals()`. The shading
normal comes back as `PathMaterialState::Ns`, which light sampling's hemisphere and the ray differentials use (ray offsets use `Ng`). The
ISPC equivalent is `PathMaterial_resolveNormal()` (accept an authored normal if finite,
non-degenerate and in the smooth normal's open hemisphere, else the smooth normal; Typhoon's
`TryResolveNormalShdWldExt()`), then `PathMaterial_faceNormal()`. Native glossy lobes must
also raise their normal towards `Ng` as `PrepareShadingNormals()` does; the only native kind
so far, a Lambertian, has none. (Superseded by tickets 25 and 26: each native kind ports
the pipeline of its own graph, `PathMatte.isph`, `PathPbm.ispc`, glossy correction
included; `PathMaterial_resolveNormal()`, kept for a while as a reference that the
OpenUSD-linked `reference_shading_normal` test checked, was removed with that test.)

## What stays with the integrator

The interface returns material facts; policy stays in the loop as Typhoon has it: the
transparent-shadow rules (which blockers transmit straight, tinting, medium ownership),
MIS weights, firefly clamps, RGB-to-spectral conversion of BSDF values, Russian roulette,
and what to do for a lane without a closure (Typhoon lights it with a display-colour legacy
closure and doesn't continue it; ticket 10 decides whether that runs through C++ or
natively).

## Considered options

- Material function pointers per subtype, like geometry hooks, instead of a kind switch:
  a varying call through a pointer runs once per distinct pointer in the gang and can't be
  inlined, and the C++ lanes must be gathered into one call anyway.
- Returning subsurface coefficients and the interior medium from every shading call: about
  thirty more floats per lane on every vertex for data few lanes need.

## Consequences

- `struct Material` has a `kind` and a `cppMaterial` pointer (80-byte slot). Matte and
  physicallyBased are `MATERIAL_KIND_LAMBERT` until ticket 11 gives them MaterialXCpp graphs
  (then `MATERIAL_KIND_CPP`); the Lambertian is a placeholder not validated against Typhoon
  (tickets 25 and 26 add validated native kinds).
- `halcyon_path_materials` (pathtracer builds) holds the interface; the device links it since
  the renderer exists (ticket 06). Typhoon's closure classification, which the flags need,
  is ported into `ClosureClassification.cpp`, since the rest of Typhoon's integrator
  directory needs OpenUSD.
- The shading point carries what ticket 03's prototype did plus a tangent frame and display
  colour; ticket 08 adds derivatives, primvars and instance transforms.
- Amended by ticket 06: the shading point also carries `baseColor`, the material's colour at
  the hit as the integrator resolves it (constant, attribute or sampler), which native kinds
  read; the Lambertian placeholder reflects it instead of the material's constant colour, so
  attribute- and sampler-coloured materials render with their colours. C++ materials don't
  read it (their graphs resolve their own inputs).
- Amended by ticket 08: the shading point carries the whole of Typhoon's
  `mxcpp::ShadingContext` that halcyon can fill (`renderer/pathtracer/ShadingContext.isph`):
  texture coordinates (the `attribute0` attribute), display colour and opacity (the `color`
  attribute, grey 0.5 and 1 without one), face id and hit coordinates, world and object
  surface derivatives, screen-space derivatives from ray differentials, and the instance
  transform and its inverse; the camera position is in `PathMaterialContext`. `baseColor`
  stays separate from `displayColor`: it is the material's colour for native kinds, while
  `displayColor` is the geometry's for the graphs' geomcolor nodes. The shadow call's C++
  side leaves the screen-space derivatives zero, as Typhoon's visibility-only evaluation
  does.
- `useAdobeOpenPBR` is the one renderer setting materials read so far
  (`PathMaterialContext`; superseded by tickets 25 and 26, which add `nativeMaterials`,
  the luminance coefficients and the view position).
- Cost (`path_material --bench`, same machine as ADR 0003): a vertex through the interface
  (shade, one eval, and the subsurface and medium calls where they apply) costs 40 to 75 ns
  per point more than the same Typhoon sequence called directly in C++, against 1.2 to
  3.6 us of C++ work. Ticket 08's whole shading context raised that (about 190 ns was
  measured then; 55 to 60 ns just before ticket 33, medians of five runs); since ticket 33
  it is 5 to 25 ns for graphs that read none of the parts sent on demand (see its
  amendment below).
- Amended by ticket 10: the shading point also carries `baseOpacity`, the material's
  opacity after `alphaMode` as the integrator resolves it, which native kinds take as their
  presence and opacity (as Typhoon's glTF PBR closure sets both from alpha): the Lambertian
  placeholder resolves presence in the shading call and reports its opacity to shadow rays.
  The integrator builds the whole shading context for the shadow call only on C++ lanes.
  A lane without a closure is lit natively (a Lambertian of `baseColor`, direct light
  only, no MIS) and its path ends, as Typhoon's.
- Amended by ticket 31: `HALCYON_ENABLE_PATHTRACER` is gone; every build has the pathtracer,
  so "pathtracer builds" above now means every build.
- Amended by ticket 11: matte and physicallyBased are `MATERIAL_KIND_CPP`, MaterialXCpp
  graphs built by `renderer/pathtracer/MaterialConverter.*` and compiled in
  `Material::finalize()`; no material is `MATERIAL_KIND_LAMBERT` any more, which stays as
  the integrator's fallback for lanes without a closure (its `baseColor` is then the
  device's `invalidMaterialColor`). `struct Material` gains `attributeMask` (the slot stays
  80 bytes), the attributes the C++ material's geomprop nodes read; the shading point
  carries them resolved at the hit (`PathShadeInput::attributeMask`, `attributes`), and
  `PathShadeInSoA::attributes` the rows in the mask, which the C++ side's geomprop lookup
  reads. The C++ shading context also gets the colour transform callback and the working
  space's luminance coefficients (`ColorManagement.*`, OpenUSD's float values, which
  differ from MaterialXCpp's defaults in the last bits). The interface cost (`--bench`) is
  unchanged within noise.
- Amended by ticket 33: the shading and shadow calls send each material only the parts of
  the shading context its graph can read. `PathContextGroup` (`PathMaterial.isph`) names
  the parts sent on demand: display colour and opacity, the primitive (face id and hit
  coordinates), the surface derivatives, the screen-space derivatives and the instance
  transforms. Everything else (positions, normals, tangent frame, `wo`, texture
  coordinates, the attributes in `attributeMask`) still goes to every material.
  `contextGroupsRead()` (`renderer/pathtracer/ContextGroups.*`) finds the parts from the
  graph's node types, through a table from an audit of every read of those fields in the
  vendored MaterialXCpp and BSDL sources, plus halcyon's sampler node (the face id, for
  primitive samplers). A node type the table doesn't list reads none, so the audit must be
  redone when the Typhoon pin moves: the `material_graph` ctest fails until the table's
  audited commit is the one in `external/typhoon/PROVENANCE.md`. The table
  over-approximates. Every node counts, reachable or not. A node type counts as reading
  whatever any of its inputs could make it read, except that a geometric node whose
  constant `space` is its own doesn't count as reading the transforms. The table also covers
  the nodes `EvalGraph::Compile()` adds for unconnected inputs (the world tangent that BSDF
  and flake nodes get reads `dPdu`). `CppMaterial::contextMask()` holds the result,
  `Material::contextMask` holds it for ISPC (in what was padding, so the slot stays 80
  bytes), and `PathShadeInSoA::contextMask` holds it per lane. ISPC writes only those
  groups into the record and C++ copies only those; the others keep
  `mxcpp::ShadingContext`'s defaults. `ShadingContext_build()` still fills every
  `PathShadeInput` member, because the integrator's ray differentials and the shading
  context's debug modes read them. Images and `path_material` results are bitwise
  unchanged. `path_material` now fills every part with distinct values, checks that a
  material reading each part gets it, and checks that sending every part to every
  material changes no bit. Interface cost (`--bench`, five interleaved runs each before
  and after, medians): emissive UsdPreviewSurface 61 to 6 ns, OpenPBR coat and fuzz 60 to
  21 ns, noise3d UsdPreviewSurface 55 to 26 ns. Single runs scatter by up to about 20 ns,
  with outliers of up to 190 ns, against C++ work of 1.2 to 3.6 us that itself varies
  from run to run by about 50 ns.
- Amended by ticket 24: for the albedo channel, a shading call with `PATH_SHADE_ALBEDO`
  also returns the closure's albedo (`PathMaterialState::albedo`, through
  `PathShadeOutSoA::ar/ag/ab`) and `PATH_MATERIAL_SPECULAR_ONLY`, from
  `closureAlbedo()` (`renderer/pathtracer/ClosureAlbedo.*`), a summary of the evaluated
  closure's BSDF tree (AGENTS.md, "Frame channels"). The integrator sets the flag only
  while a path hasn't reached its first non-specular hit and the frame has an albedo or
  normal channel, so other renders cost nothing more. Native kinds always report their
  reflectance as their albedo.
- Amended by ticket 25: matte is `MATERIAL_KIND_MATTE`, the first validated native kind
  (`renderer/pathtracer/PathMatte.isph`, a port of what Typhoon's C++ does for its graph,
  `ND_oren_nayar_diffuse_bsdf` of roughness 0 under `ND_surface`): presence, normals
  (including `ValidateLeafNormals()`' renormalized leaf normal and bump shadowing against
  the smooth one), flags, `SampleLambertian()` in `Frame::FromNormal()`, eval, the shadow
  call's facts and the albedo summary, from `PathShadeInput::baseColor` (now matte's colour
  at the hit, `Material_getColor()`, for matte lanes) and `baseOpacity`; a matte whose
  parameters those readers would read otherwise than its graph does (a FLOAT32 `color`, a
  vector `opacity`, outside the specification) stays `MATERIAL_KIND_CPP`.
  `PathMaterialState` gains `lobeN` and `color` (the native lobe's normal and colour).
  Native kinds keep their C++ material (`Material::cppMaterial`, compiled as before), and
  `PathMaterialContext::nativeMaterials` (the `quality` renderer's `nativeMaterials`,
  default true, declared in `HalcyonDefinitions.json`) false makes `PathMaterial_kind()`,
  which now takes the context, send them through it: both run on the same scenes and
  sample sequences. Native matte matches its graph within rounding (1e-4 relative per
  output, `path_material`; ISPC's `sin`/`cos` and fused multiply-adds), and renders agree
  per pixel except where a direction a few ulps apart crosses an edge (`native_materials`:
  2 of 4096 pixels beyond 1e-4, mean difference 1.2e-5, albedo and normal identical).
  `MATERIAL_KIND_LAMBERT` stays as it was, only the integrator's fallback for surfaces
  without a closure: it isn't any material's port (its sampling and its rejection of
  directions below `Ng` in eval differ from Typhoon's diffuse leaf), and keeping it
  leaves images of such surfaces unchanged. Speedup (48 threads, AVX-512): a vertex
  (shade, one eval, subsurface and medium calls) 500 to 42 ns per point
  (`path_material --bench`, about 12×); a 512 × 512, 4-sample, `maxBounces` 16 render of
  `native_materials`' matte box with two C++ spheres 271 to 137 ms (2.0×, medians of 7; 1.9×
  on one thread).
- Amended by ticket 26: physicallyBased is `MATERIAL_KIND_PHYSICALLY_BASED` where
  `nativePhysicallyBasedCovers()` (`renderer/pathtracer/MaterialConverter.h`) admits its
  parameters (no thin film; any source for every other parameter, normal maps
  included) and its colour and opacity have the types native matte requires; otherwise it stays `MATERIAL_KIND_CPP`, decided per
  material at every commit (AGENTS.md, "Materials in the pathtracer", has the exact
  rule). It ports what Typhoon's C++ does for the glTF PBR closure those parameters
  give: the closure tree's traversal (lobe choices, finalization, evaluation and pdfs of
  a clearcoat layer over a sheen layer over a mix of a dielectric layer (over a mix of
  the Lambertian and the transmission) and a metal), its lobes (generalized Schlick and
  dielectric GGX reflection and GGX transmission, rough and delta, with VNDF sampling,
  refraction and total internal reflection; Charlie sheen; the Lambertian), Turquin's compensation and the layer throughputs from
  the vendored GGX energy and BSDL dielectric filter tables, read in place, the normal
  maps (`ND_normalmap_float` of the samplers' values, as the graph builds them, and bump
  shadowing against the smooth normal), caustic pruning, the glossy normal correction,
  the flags, emission, the shadow call's facts (with the straight transmission, for
  which the shadow call resolves native physicallyBased's parameters too) and the
  albedo summary. The material's
  parameters beyond the colour and opacity reach ISPC as `Material::physicallyBased`
  (`PhysicallyBasedParameters` of `MaterialParameter`s: constant, attribute or sampler,
  and the normal maps' samplers; the material slot grows from 80 to 96 bytes) and are
  resolved per hit as the graph's inputs into the new `PathShadeInput::pbm`;
  `PathMaterialState::pbm` holds what eval needs (which parts the closure has at the
  vertex, and its weights and throughputs, which depend on wo alone);
  `PathMaterialContext::luminance` (the renderer's, from `luminanceCoefficients()`)
  weighs lobes as the C++ trees do. Shade and eval live in `PathPbm.ispc`, compiled with
  `--opt=disable-fma --math-lib=system`, so they round as the C++ does: native and graph
  results agree bitwise (`path_material`; `path_pbm` per lobe function against the
  vendored C++), and renders with `nativeMaterials` true and false are identical
  (`native_materials`). Within rounding was not enough here: a sharp GGX lobe turns a few
  ulps of difference in a direction into differences of percents in its value. Speedup
  (48 threads, AVX-512 gang of 16): a vertex 1680 to 148 ns per point (ANARI's default
  rough metal; 2100 to 170 ns for a half metal, 2980 to 181 ns with a sheen and 4340 to
  225 ns with a clearcoat and normal maps; 11–19×; the system's math library costs about
  45 ns of each), and a 512 × 512, 4-sample, `maxBounces` 16 render of
  `native_materials`' physicallyBased box (every surface native but an iridescent
  sphere) 450 to 120 ms (3.75×, medians of 7).
- Amended by ticket 47: C++ lanes sample through `sampleSurface()` (`ClosureSampling.*`,
  ADR 0003's amendment), which passes a mix's delta samples up unscaled; native
  physicallyBased weighs its two mixes the same way, so native and graph stay bitwise.
- Amended by tickets 49 and 52: `PathShadeInput::Nsrf` is turned into `Ng`'s hemisphere,
  and both normals are taken to world space as Typhoon does, in double with each
  component rounded (`Transport_surfaceNormalToWorld()`).
- Amended by ticket 50: `PathShadeInSoA::attributePresent` says which attributes in the
  mask the hit has; `lookupGeomProp()` returns nothing for one it lacks, so the geomprop
  node gives its `default`.
- Amended by ticket 51: a material's `contextMask` also has `PATH_CONTEXT_OBJECT_POSITION`
  where the compiled graph reads the object position (`RequiresObjectSpacePosition()`),
  mirrored in `Material::attributeMask`'s high bit (`MATERIAL_READS_OBJECT_POSITION`); the
  shading context then takes the object position from the geometry's
  `getSurfacePosition` hook.
