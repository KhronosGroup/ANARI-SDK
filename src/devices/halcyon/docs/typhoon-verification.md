# Verification of the pathtracer against Typhoon and the ANARI test tools

**A record.** The tools this report used (the `tests/typhoon_compare/` scripts and
programs, the USD translator `usd_scene.py` and the OpenUSD-linked reference harness) were
removed so that halcyon carries no OpenUSD dependency on its way into VisRTX; `e80511e` is
the last commit with them. The results below are as last measured, and nothing here can be
run from this tree: the commands, scripts and files named are those of `e80511e`, the
renderer names those of the time (the renderer then called `default` is now `preview`, the
one called `pathtracer` now `quality`), and paths under `~` are those of the machine the
sweep ran on.

> **Tracker references.** This record predates halcyon's move into VisRTX: its ticket
> and follow-up numbers ("pathtracer ticket N", "follow-up N") refer to halcyon's
> original private tracker, and its commit hashes to the deprecated `anari-halcyon`
> repository (see AGENTS.md, Overview).

Pathtracer ticket 28's sweep, run 2026-10-06 at halcyon `702d015` plus this report's
commit, and rerun the same day with ticket 47's change (a mix's delta samples, AGENTS.md
"Materials in the pathtracer"; only disneyPrincipled_parameter_parity changed, result 5;
`media.sh`, `spectral.sh` and `subsurface.sh` still pass), and again with ticket 48's
(media left on any outward exit, inward shadow rays in the medium, delta suns found only by
light sampling; AGENTS.md "Interior media" and "Lights"; the rerun also covers
`5848237`, ticket 47's review fixes, which landed after ticket 47's sweep):
`halcyon_compare_usd` on the Typhoon images and translations of ticket 47's sweep, reused
(Typhoon, its settings and `usd_scene.py` are unchanged), prints the same numbers for
every scene but sss_randomwalk_gallery's image z (+1.47 → +1.46), and of the three
scripts (Typhoon rendered anew) only `media.sh`'s standard_surface cube moves, by 0.002%
(no scene of the sweep has a delta distant light: normalmap_bump_h2n's has an angle of
1°). Ticket 49 (shading normals turned into `Ng`'s hemisphere, AGENTS.md "Rendering
pipeline") reran the sweep in full, Typhoon rendered anew, with `usd_scene.py` no longer
turning normals to the winding's side (see below), and the three scripts with their
grounds as authored: every number is the same as before, to this report's precision.
Ticket 50 (a geomprop node reading an attribute the hit lacks gives its `default`,
AGENTS.md "Shading context", "Primvars") reran the sweep in full, Typhoon rendered anew,
with `usd_scene.py` no longer writing those defaults as attributes (see below): every
number is again the same as before, to this report's precision.
Ticket 51 (graphs that read the object position get it from the mesh, AGENTS.md "Shading
context", "Object position") reran the sweep in full, Typhoon rendered anew: only two
scenes change, both with such graphs: procedural3d_gallery, which now passes (result 3), and
openPbr_bump_fractal, whose ground's fractal bump reads the position (image −0.043% →
−0.033%, z −0.6 → −0.4); every other number is the same as before, to this report's
precision.
Ticket 52 (the pathtracer's smooth normal normalized as Typhoon's, in double with each
component rounded, AGENTS.md "Shading context", "The smooth normal") reran the sweep in
full, Typhoon rendered anew: microfacet_ms_smoke and its OpenPBR variant lose their
offsets (below), cornell_box's and cutglass_dispersion_compare's worst regions move within
their noise (cutglass's −0.17% → −0.15%, noise 1.004 → 1.001), and a few image means move
in their last digit; every other number is the same as before, to this report's
precision. `media.sh`, `spectral.sh` and `subsurface.sh` still pass (ticket 52's
measurements below). Ticket 52's review then ported `Ng`'s normalization and Typhoon's
second normalization of both normals; the sweep and the three scripts were rerun once
more (the scripts all pass): only cutglass_dispersion_compare's worst region and
microfacet_ms_smoke's last digits moved, as the table gives.
Ticket 53 (translucent samples enter interior media and aren't caustic samples, AGENTS.md
"Interior media" and "Dispersion and caustics") wasn't rerun against Typhoon: no scene of
the sweep or the three scripts has a translucent lobe, and halcyon renders every one of
them (the sweep's 46 through `halcyon_compare_usd`, the scripts' 14 through their
`halcyon_compare_*` programs, both seeds at 16 samples) bitwise as the library before
it, so every number here stands.
Typhoon at the pin (`70c45e8`, `~/opt/.install/OpenUSD-typhoon-70c45e8`) and the ANARI
SDK 0.17.0 tools (`~/opt/anari/bin`). Everything here was measured with the scripts named,
which are no longer in the tree (see the note at the top); none of it was a ctest.

## How the scenes are compared

Typhoon's MaterialXCpp test scenes
(`TY/renderer/materials/MaterialXCpp/tests/data/usd/*.usda`) are rendered by both renderers
at the same seeds (1 and 2) and sample count, fixed (adaptive sampling off), and compared
against the noise between seeds (`tests/typhoon_compare/compare_common.h`, as the
subsurface, media and spectral comparisons already did):

- `tests/typhoon_compare/usd_scene.py` (OpenUSD's Python, test-only) translates a scene for
  halcyon as Typhoon's Hydra delegate sees it: meshes fan-triangulated, implicit spheres,
  cubes and cylinders as the meshes `HdsiImplicitSurfaceSceneIndex` makes of them, smooth
  normals computed as `Hd_SmoothNormals` does where Typhoon computes them, the primvars
  materials read (as `attribute0`–`3` and `color`), lights (UsdLux radiance, as Typhoon
  computes it), the camera conformed to the image as usdrender does and its exposure, the
  scene's `ty:` settings. Each bound UsdShade network becomes a MaterialX document for a
  `materialx` material (one node per shader, by nodedef; USD colour space names mapped to
  MaterialX's, which the subtype maps back). It also writes the session layer both
  renderers see the scene through (a camera where the scene has none, render settings
  naming it, a product of the comparison's resolution).
- Typhoon renders with `usdrender --complexity low`: halcyon has no subdivision surfaces
  (ticket 27), and usdrender's default (`high`) has Typhoon subdivide catmullClark meshes,
  the implicit spheres among them.
- `halcyon_compare_usd` (`tests/typhoon_compare/compare_usd.cpp`) builds the translation
  through ANARI, renders it with the `pathtracer` and prints, per region (the whole image
  and each mesh's pixels) and channel, the difference of the means in standard errors and
  relatively, and the RMS difference across seeds against Typhoon's own (`noise`, 1 when
  the renderers agree) and at the same seed (`same-seed`, far below 1 where the paths
  agree: both draw the same samples). A scene passes if every region is within 4 standard
  errors or 0.5%, and `noise` is within 10% of 1 (without the largest 0.1% of pixel
  differences). Regions beyond 4 standard errors that pass on the 0.5% rule alone are
  listed as `offset` (see below).
- `tests/typhoon_compare/scenes.sh <build> [spp] [out] [scene ...]` runs it all and writes
  `summary.txt`.

The comparison sets the pathtracer's `convergedSamplesPerPixel` to the sample count, as
Typhoon's `ty:convergedSamplesPerPixel`, since both scale ray differentials (and so texture
footprints) by it.

### Former adaptations

Until tickets 49 and 50, halcyon differed from Typhoon by an open decision (see
[Decisions](#decisions-for-the-maintainer)) in every scene that exercised it, and so that
the rest of each scene could be compared, `usd_scene.py` adapted the translation and listed
every mesh it adapted (`adapted.txt`, an `adapted` column of the results). Both decisions
have landed; the translation adapts nothing, and the column is gone:

- **Primvars a mesh lacks** (ticket 50). A MaterialX `geompropvalue` (or UsdPrimvarReader)
  whose primvar a mesh doesn't carry gives its `default` in Typhoon, and gave halcyon's
  attribute fallback, (0, 0, 0, 1), when the primvar's attribute was a known name; the
  translation wrote the default as a constant attribute (geompropvalue_test's 2 meshes,
  normalmap_bump_h2n's 3). halcyon's geomprop nodes now give their default where the hit
  lacks the attribute, the translation hands halcyon the meshes as authored, and both
  scenes give the same results as with the adaptation, to the precision this report gives.
  Only normalmap_bump_h2n tells the two rules apart: its cubes lack the texture scale its
  `geompropvalue` reads (default (3, 3)), where geompropvalue_test's `PlainPlane` reads
  defaults of 0, which (0, 0, 0, 1) also gives.
- **Smooth normals against the winding** (ticket 49). The translation turned corner
  normals that point against a triangle's winding to its side, as face-varying normals:
  Typhoon turns the smooth normal into the geometric normal's hemisphere at every hit,
  and halcyon didn't, so such meshes shaded black (cornell_box's walls, most ground
  planes; 17 of the 46 scenes). halcyon now turns the interpolated normal at the hit as
  Typhoon does (AGENTS.md, "Two normals"), and the translation hands it the authored
  normals: those 17 scenes give the same results as with the adaptation, to the precision
  this report gives, also the curved meshes whose triangles mix turned and unturned
  corners, where the adaptation (which turned corners before interpolation) was only close
  (the cutglass mesh, surface_derivatives_test's three meshes).

Two deliberate differences recorded in AGENTS.md are also hidden by the translation, which
does what Typhoon's Hydra delegate does:

- **Smooth normals for meshes without them.** halcyon shades a mesh without
  `vertex.normal` or `faceVarying.normal` with Ng, where Typhoon computes smooth normals
  (AGENTS.md, "Shading context", "the pathtracer shades such meshes with Ng too"). The
  translation computes them as `Hd_SmoothNormals` does and hands them to halcyon as
  `vertex.normal`, so the sweep doesn't compare the faceted shading.
- **Meshes without a material.** halcyon shades them in the device's
  `invalidMaterialColor` (magenta by default), Typhoon as a Lambertian of their display
  colour (AGENTS.md, "The quality renderer"). The comparison sets
  `invalidMaterialColor` to that colour (the scenes have one such colour).

## Results (256 samples per pixel, 320 pixels wide)

| scene | result | image | worst region | noise | same-seed | gaps |
|---|---|---|---|---|---|---|
| caustics_off_policy_regression | PASS | -0.015% (z -1.3) | ThickSlabReceiver/b +0.04% (z +1.4) | 0.979 | 0.364 | 0 |
| caustics_off_policy_regression (caustics on) | PASS | -0.020% (z -1.3) | RoughGlassSphere/b -1.1% (z -1.4) | 0.994 | 0.253 | 0 |
| color_management_lin_rec709 | PASS | +0.001% (z +1.0) | TextureCardB/g +0.002% (z +3.0) | 0.999 | 0.012 | 0 |
| color_management_mixed | PASS | +0.001% (z +1.7) | TextureCardB/g +0.004% (z +6.8) | 0.999 | 0.021 | 0 |
| colortransform_gallery | explained (1) | -0.78% (z -176) | Rec709DisplayCard/r -1.6% (z -94) | 2.854 | 2.779 | 0 |
| cornell_box | PASS | -0.002% (z -0.07) | GlassSphere/b -0.02% (z -0.09) | 1.000 | 0.012 | 0 |
| cutglass_dispersion_compare | PASS | -0.004% (z -0.8) | cutglass/r -0.17% (z -0.7) | 1.000 | 0.296 | 0 |
| disneyPrincipled_parameter_parity | explained (5) | -0.19% (z -6.3) | TransmissionGlass/b -3.8% (z -7.0) | 1.023 | 0.152 | 0 |
| dpdu_dpdv_debug | PASS | -0.000% (z -0.07) | Sphere_Subdiv/r (z -0.1) | 1.000 | 0.000 | 0 |
| emissive | PASS | -0.000% (z -0.3) | Sphere/b (z -0.3) | 1.000 | 0.000 | 0 |
| eon_diffuse_openpbr_compare | PASS | +0.000% (z +0.07) | Roughness05/b (z -0.9) | 1.003 | 0.290 | 0 |
| furnace_material_models | PASS | -0.000% (z -0.01) | SphereOpenPbr/r (z +0.04) | 1.000 | 0.006 | 0 |
| geompropvalue_test | PASS | -0.000% (z -0.05) | PlainPlane/r (z -0.1) | 1.007 | 0.001 | 0 |
| gltfPbr_parameter_parity | PASS | -0.001% (z -0.05) | AnisoRotation0/g (z -0.1) | 0.999 | 0.002 | 0 |
| ies_profile_gallery | not comparable (2) | -92% | ReceiverWall | 48.5 | 48.5 | 24 |
| microfacet_ms_smoke | PASS | +0.000% (z +0.1) | Silver_R005/r -0.02% (z -0.8) | 1.001 | 0.089 | 0 |
| microfacet_ms_smoke_openpbr | PASS | +0.000% (z +0.2) | Gold_R005/b -0.01% (z -0.6) | 1.000 | 0.088 | 0 |
| multi_material | PASS | -0.008% (z -0.5) | SphereRight/r (z -1.0) | 1.008 | 0.549 | 0 |
| nd_usd_nodes_gallery | PASS | -0.020% (z -0.4) | TexturedCard/r (z -1.4) | 1.009 | 0.393 | 0 |
| nodegraph_mix | PASS | +0.000% (z +0.2) | Sphere/r (z +0.2) | 1.013 | 0.001 | 0 |
| nodegraph_multiply | PASS | +0.000% (z +0.06) | Sphere/b (z +0.06) | 1.001 | 0.001 | 0 |
| nodegraph_ramp | PASS | -0.000% (z -0.1) | Plane/g (z -0.08) | 0.999 | 0.001 | 0 |
| normalmap_bump_h2n | explained (1) | -0.001% (z -2.3) | GroupB Cube/r -0.53% (z -22) | 1.327 | 0.949 | 0 |
| npr_nodes_sphere_gallery | PASS | +0.000% (z +0.02) | FacingratioSphere/r (z -0.2) | 1.002 | 0.001 | 0 |
| openPbr_bump_fractal | PASS | -0.033% (z -0.4) | GroundPlane/r (z -0.5) | 0.992 | 0.364 | 0 |
| openPbr_glass | PASS | -0.001% (z -0.2) | Ground/b (z -0.2) | 1.003 | 0.001 | 0 |
| openPbr_parameter_parity | PASS | -0.001% (z -0.04) | NormalOverride/b (z -0.07) | 0.996 | 0.001 | 0 |
| openPbr_simple | PASS | +0.000% (z +0.09) | Sphere/g (z +0.09) | 1.003 | 0.001 | 0 |
| openpbr_sss_native_adobe_compare | PASS | +0.11% (z +2.8) | RoughSpecularPureSss/b +0.36% (z +2.0) | 0.996 | 0.360 | 0 |
| procedural2d_gallery | PASS | +0.017% (z +0.8) | Flake2d/r +1.1% (z +0.9) | 0.998 | 0.121 | 0 |
| procedural3d_gallery | PASS (3) | -0.16% (z -1.1) | Flake3d/g -7.7% (z -1.3) | 1.003 | 0.071 | 0 |
| rough_dielectric_white_furnace_sphere | PASS | +0.000% (z +0.04) | Sphere/r (z +0.04) | 1.000 | 0.000 | 0 |
| sss_randomwalk_gallery | PASS | +0.019% (z +1.5) | SphereOpenPbrSss/b +0.7% (z +1.8) | 0.999 | 0.029 | 0 |
| standardSurface_coated | PASS | +0.000% (z +0.02) | Sphere/r (z +0.02) | 1.006 | 0.003 | 0 |
| standardSurface_simple | PASS | +0.000% (z +0.05) | Sphere/b (z +0.05) | 1.002 | 0.001 | 0 |
| subdiv_facevarying_test | PASS | +0.001% (z +0.8) | SphereOpenPBR/r (z +0.6) | 0.995 | 0.308 | 0 |
| subdiv_st_interpolation_compare | PASS | +0.000% (z +0.01) | SphereVertexST/b (z +0.09) | 0.999 | 0.000 | 0 |
| surface_derivatives_test | PASS | -0.18% (z -3.6) | GlassSphere/g -2.1% (z -3.4) | 0.978 | 0.357 | 0 |
| surface_shader_gallery | PASS | -0.000% (z -0.01) | SpherePreviewNative/g (z -0.05) | 1.000 | 0.002 | 0 |
| texture_image_compare_gallery | PASS (1) | -0.046% (z -31) | CheckerTiledCard/r -0.14% (z -39) | 1.035 | 0.338 | 0 |
| transparency_material_gallery | PASS | -0.003% (z -0.3) | SphereOpenPbrThick/b (z -0.5) | 1.001 | 0.031 | 0 |
| transparency_material_gallery (caustics on) | PASS | -0.004% (z -0.4) | SphereOpenPbrThick/b (z -0.5) | 1.001 | 0.030 | 0 |
| usd_native_nodes_gallery | PASS (4) | -0.022% (z -0.4) | TexturedCard/b (z +1.0) | 1.009 | 0.392 | 2 |
| usdPreviewSurface_metallic | PASS | -0.000% (z -0.04) | GoldSphere/g (z -0.04) | 1.002 | 0.001 | 0 |
| usdPreviewSurface_simple | PASS | +0.000% (z +0.2) | Sphere/r (z +0.2) | 1.013 | 0.001 | 0 |
| volume_transmission_single_cube | PASS | +0.040% (z +1.0) | TransmissionCube/r +0.24% (z +1.0) | 1.004 | 0.028 | 0 |

42 of 46 pass; the other four are explained:

1. **Image texture minification** (colortransform_gallery, normalmap_bump_h2n's image bump
   and height-to-normal groups, texture_image_compare_gallery's tiled checker): halcyon's
   image files filter through a box mip chain, trilinearly, Typhoon's OIIO backend
   otherwise (known, ADR 0005). It shows where 2k images cover a few dozen pixels, most
   after a gamma-encoded image is decoded (filtering before the decode makes blurrier
   images darker). At 4× the resolution (1280 pixels wide) the differences fall about
   tenfold (colortransform_gallery's worst card 2.9% → 0.26% at 64 samples,
   normalmap_bump_h2n's 2.6% → 0.29%).
2. **IES profiles, colour temperature, `normalize` on disk lights** (ies_profile_gallery):
   halcyon has no IES (ANARI has no counterpart; `intensityDistribution` is not an IES
   file) nor colour temperature, so the scene can't be translated. Not comparable.
3. **Object position on a cell boundary** (procedural3d_gallery's `randomcolor` and
   `cellnoise3d` planes; resolved by ticket 51): the planes lie at object-space y = 0,
   where `cellnoise3d` of the position changes cell. Typhoon re-interpolates the object
   position from the mesh for graphs that read it; halcyon took the hit's, a few ulps off
   the plane, so it picked the neighbouring cell's random colour on some pixels
   (`Randomcolor/g` +9.3%, z +44; the image +0.79%, z +11). halcyon now does as Typhoon
   (AGENTS.md, "Shading context", "Object position"), and the scene passes: the image
   −0.16% (z −1.1), every region within 1.3 standard errors (the worst `Flake3d`'s sphere,
   −7.7% of a dark region), the RMS difference across seeds 1.003 times Typhoon's own and
   0.071 at the same seed (3.6 before).
4. usd_native_nodes_gallery's `MatrixCard` reads a matrix primvar (`UsdPrimvarReader_matrix`):
   ANARI attributes are at most four floats. The card is left out of both renderers'
   scenes (the session layer hides it from Typhoon); the rest passes.
5. **Delta lobes in mixes** (disneyPrincipled_parameter_parity's `TransmissionGlass`, a
   deliberate difference since ticket 47): disney_principled puts its transmission, a
   delta dielectric (roughness 1e-5), in a mix of weight `specTrans` (0.95 here), and
   Typhoon divides a mix's delta samples by the mix's probability, so its transmission is
   1 / 0.95 too bright; halcyon's is 3.1–3.8% darker on the sphere (z −4 to −7), 0.19% on
   the whole image. With the library before ticket 47 the same scene passes (worst
   −0.13%, z −0.2; `halcyon_compare_usd` on the same Typhoon images), so this is the
   change alone. No other scene of the sweep has a delta lobe in a mix: every other row
   is unchanged.

### Regions beyond the noise within 0.5%

The pass rule (4 standard errors or 0.5%) lets a systematic offset of under 0.5% pass once
the noise resolves it. `halcyon_compare_usd` lists such regions as `offset` and counts them
in the summary (`offsets`). At 256 samples they are:

- **Image textures** (explained, result 1's minification): color_management_mixed's
  `TextureCardB` (+0.004%, z up to 6.8), colortransform_gallery's linear-space cards
  (−0.03 to −0.12%, z 4–8), normalmap_bump_h2n's image normal-map and height-to-normal
  groups (A and D, cubes and spheres, ±0.1–0.3%), texture_image_compare_gallery's cards
  and whole image (−0.14% to +0.10%, z up to 39).
- **Resolved (ticket 52): the lowest-roughness metals of microfacet_ms_smoke** (and its
  OpenPBR variant): `Gold_R005` and `Silver_R005` were 0.32–0.39% darker in halcyon in
  every channel (z −13 to −17); since ticket 52 they agree (worst −0.02%, z −0.8). See
  decision 5 for the cause and the evidence.

Some scenes author shader inputs with types USD doesn't register (`vector3`) or connect a
`vector3` output to a `color3` input; Typhoon's network doesn't get the former and MaterialXCpp
takes the latter, and the translation does the same (its log notes them; the halcyon
material reports a validation warning for the connection).

Typhoon's two `*_adobe.usda` variants turn on Adobe's OpenPBR, which neither Typhoon's
build (`PXR_ENABLE_ADOBE_OPENPBR_SUPPORT` off) nor halcyon has; they aren't rendered. The
four `*.session.usda` files are used as session layers above.

The textured dome (follow-up 14, item 3) is covered twice: `reference_lights` now checks the
sampling distribution, `SampleDomeLight()` and `EvaluateDomeLightDirection()` of textured
maps against Typhoon's, and cutglass_dispersion_compare and volume_transmission_single_cube
render the StinsonBeach dome (turned, in cutglass) in this sweep.

## Translation gaps (scenes need ANARI-native equivalents)

- **hdAnari** has no MaterialX path: a `materialx` network becomes its default matte, and
  UsdPreviewSurface becomes `physicallyBased` (glTF PBR, a different BSDF from Typhoon's
  UsdPreviewSurface). Its light translation also differs from the spec halcyon follows
  (quad front, rect `intensity`, rect textures, disk `outerRadius`;
  follow-up 13). So the sweep drives halcyon through `usd_scene.py`, not hdAnari.
- The hdAnari installed with OpenUSD 26.05 (`~/opt/.install/OpenUSD-26.05/plugin/usd/
  hdanari_rd.so`, built 2026-07-18 from an older ANARI-SDK) renders black under
  `usdrecord --disableGpu` with helide and halcyon alike ("channel.color buffer is not
  present"), and Typhoon's `usdrender` needs the delegate to write its products, which
  hdAnari doesn't. Driving halcyon through hdAnari needs an hdAnari built from
  `~/src/claude/ANARI-SDK` (`-DBUILD_HDANARI=ON -Dpxr_DIR=~/opt/.install/OpenUSD-26.05`,
  installed into its own `plugin/usd` on `PXR_PLUGINPATH_NAME`) and a headless path
  (`usdrecord`) that maps its AOVs.
- Not translated: subdivision (compared at the control cage), IES, colour temperature,
  `normalize` on finite lights, light shaping cones (none outside the IES scene), matrix and
  string primvars, geometry subsets, point instancers, more than three primvars per
  material besides `st` (none in the scenes).

## ANARI tools

Run against the build directory (`LD_LIBRARY_PATH=<build>:~/opt/anari/lib`), with the helide
installed then, before the helide fixes of ANARI-SDK 0.17 landed (AGENTS.md "Deliberate
differences from helide"): helide's `volumeSamplingRate` default was still 1 and its rays
went through pixel corners, so against helide 0.17 these counts would differ.

- **`anariInfo -l halcyon`**: the extensions, subtypes, frame channels and parameters match
  `HalcyonDefinitions.json` and AGENTS.md (both renderers' parameters, the light and
  material subtypes, `materialx`/`openPBR`/`usdPreviewSurface`). One oddity, from the SDK:
  `ANARI_VOLUME` lists an empty subtype with an `id` parameter, because the registry's
  `khr_frame_channel_object_id.json` declares the volume's `id` on a volume of name `""`
  (helide, without that extension, doesn't show it).
- **`anariRenderTests -l halcyon`** (all 14 scenes, `default` and `--renderer pathtracer
  --num_samples 16`): every scene renders, no errors or warnings.
- **`anariCts`** (329 cases; ground truth from helide, `anariCts generate --device
  helide`):
  - `default` renderer, with `-p ambientOcclusion=false` (comparisons with helide turn
    halcyon's ambient occlusion off, AGENTS.md "Deliberate differences"): 263 passed, 15
    failed, 51 skipped. The failures are deliberate differences from helide:
    `progressive_rendering` (the `default` renderer isn't progressive), 5 alpha-mode cases
    (halcyon composites `blend` and skips `mask`) and 9 volume cases, which pass with
    helide's `volumeSamplingRate` 1 (`-p volumeSamplingRate=1`: 10 of 10). With ambient
    occlusion on (the default), 6 triangle and quad `*_normal_tangent` cases fail too
    (257 passed, 21 failed). The 43 light cases are skipped: helide has no lights, so
    there is no ground truth.
  - `pathtracer`: 94 passed, 184 failed, 51 skipped. Every failure is the colour channel
    against helide's eye-lit ground truth (a path-traced image isn't); depth, ids,
    albedo and normal channels, the completion callback and `progressive_rendering` pass.
    The light cases, generated with halcyon's `pathtracer` as the device (`generate
    --device halcyon -r pathtracer --filter 'light/*'`), render all 43 without errors;
    there is no reference to score them against.

## Decisions for the maintainer

These came up in the sweep; halcyon's behaviour is unchanged but for items 1, 2, 4 and 5.

1. **Smooth normals against the winding** (follow-up 16, item 3, recorded for ticket 28; decided
   2026-10-06: Typhoon's rule in both renderers, landed with ticket 49). Typhoon turns the
   smooth normal into the geometric normal's hemisphere at every hit; halcyon didn't, and
   such surfaces rendered black (17 of the 46 scenes have such meshes, cornell_box's walls
   and most ground planes). halcyon now turns it in both renderers, and the comparison's
   adaptation is gone.
2. **A missing primvar gives the attribute fallback, not the reader's default** (severity:
   medium; decided 2026-10-06: the node's default, landed with ticket 50).
   `geompropvalue` and UsdPrimvarReader nodes in `materialx` graphs read halcyon's
   (0, 0, 0, 1) where the mesh lacked the attribute, where MaterialX and Typhoon give the
   node's `default`. halcyon now passes the attribute's presence to `lookupGeomProp()`,
   so such nodes give their default; the geomprop nodes halcyon builds itself (every
   subtype's attribute parameters, `materialx` attribute overrides, samplers'
   `inAttribute`) keep (0, 0, 0, 1) as their default, so they still match the `default`
   renderer and the native kinds. The comparison's adaptation is gone.
3. **Camera exposure** (severity: low). Typhoon scales its image by the USD camera's
   exposure (normalmap_bump_h2n's camera has `exposure:responsivity` 1.10); ANARI cameras
   have none, and hdAnari applies its own exposure setting. The comparison scales halcyon's
   image; nothing for halcyon unless an app wants a camera exposure.
4. **Object position re-interpolation** (severity: low; decided 2026-10-06: port it,
   landed with ticket 51): see result 3.
5. **Low-roughness metals 0.4% darker than Typhoon's** (severity: low; investigated in
   ticket 52 and fixed in halcyon; the ill-conditioning itself is Typhoon's, recorded here
   (maintainer decision)). microfacet_ms_smoke's
   roughness-0.05 metals (GGX alpha 0.0025) on the `uni` mesh under a white dome were
   0.32–0.39% darker than Typhoon in every channel. The cause is the last bits of the
   shading normal's length, which Typhoon's GGX evaluation is ill-conditioned in just
   above its delta threshold (alpha 1e-3), and which halcyon rounded differently:
   - **Same samples, every pixel.** At the same seed 92% of the gold sphere's pixels were
     darker, by a median 0.33%: a per-sample bias, not diverging paths. On Gold_R005
     alone, `maxBounces` 0, 1, 2 and 3 give −25%, −1.5%, −0.49% and −0.40% (the first ones
     Typhoon's emitter-only segment, a recorded difference) and 4 to 64 the same
     −0.36%, so neither the emitter-only segment nor deeper paths. The firefly clamp off,
     4 light samples per hit and BSDF sampling alone (halcyon built with next-event
     estimation's weight 0 and the miss's 1, for the experiment) give the same mean, so it
     isn't MIS with the dome either.
   - **Not the mesh.** A single smooth-shaded sphere (Typhoon's implicit sphere, 92
     vertices) of the same gold was 0.51–0.55% *brighter* in halcyon, every channel by the
     same ratio, and a row of white metal spheres of roughness 0.02, 0.05, 0.1, 0.2 and
     0.35 differed only at 0.05 (+0.56%; 0.02 is a delta lobe, +0.03%; 0.1 +0.05%).
     Flat white metal planes (vertex normals equal to `Ng`, one rotation) of roughness
     0.03 to 0.08 differ by +2.1% (0.04), +1.3%, +0.89% (0.05), +0.61%, +0.43%, +0.24%
     and +0.13% (0.08): about 1 / alpha², and symmetric about the true albedo, which the
     vendored library gives as 0.9908 at both 0.04 and 0.05 for unit normals (halcyon
     1.001 at 0.04, Typhoon 0.980).
   - **The normal's length.** With the vendored closure alone (`EvalStandardSurface()`,
     `PrepareShadingNormals()`, `SampleSurface()`; a white metal at roughness 0.04 seen at
     39°), scaling the shading normal by 1 + 1.2e-7 (one ulp) raises the mean sample
     weight from 0.9908 to 1.081, by 1 − 6e-8 lowers it to 0.947 and by 1 − 1.2e-7 to
     0.907; scaling `Ng`, ω_o or the normals in the interaction instead changes nothing.
     The leaves take the normal as unit length (`PrepareShadingNormals()` hands it to
     each leaf as is, `Frame::FromNormal()` builds an unnormalized frame on it), and
     `GGX_D()`'s 1 − NdotH² cancels where NdotH² is within alpha² of 1, so an ulp of
     length moves the peak by percents.
   - **The renderers' rounding.** Typhoon normalizes the world-space smooth normal in
     double and rounds each component (`TryNormalizeDirection()`), whose length errs to
     both sides; halcyon normalized it in float (`normalize()`), which on average erred to
     one side: over the pixels the C++ shading saw, its normal's length minus 1 averaged
     +1.35e-8 on the planes (every one long), +1.7e-8 on the 92-vertex sphere and −9.9e-9
     on the `uni` mesh (64% short), the sign of each difference above.
   - **Fixed in halcyon:** `ShadingContext_build()` takes `Ng` and the smooth normal to
     world space as Typhoon's `_TryBuildSurfaceInteraction()` does
     (`TransformNormalToWorld()`, then `TryNormalizeDirection()` again, which isn't
     idempotent: about 1% of normals change by an ulp), and the derivative hooks take the
     smooth normal back in object space by `TransformNormalToObject()`, as Typhoon's
     (AGENTS.md, "Shading context", "The smooth normal"). Gold_R005 alone then agrees
     (−0.004%, z −0.26), the 92-vertex sphere renders as Typhoon's to the digits printed
     (its interpolated normals now round as Typhoon's), and the sweep's two scenes pass
     without offsets (worst −0.02%, z −0.8). The ctest `normal_rounding` checks, on a
     turned and scaled tessellated sphere of white metal under the ambient light, that the
     shading normal's mean length is 1 within 2e-9 (3e-11; −1.9e-8 with a float
     `normalize()`), and that its central disc renders at roughness 0.04 as at 0.08 within
     0.4% (1.0002; 0.986 with a float `normalize()`).
   - **What remains, Typhoon's** (maintainer decision: recorded here, not reported): the
     GGX lobes' dependence on the normal's last bits itself. A flat surface has one
     normal, so one rounding decides its albedo at such roughness in either renderer, and
     the two can't agree bit for bit there: ANARI hands halcyon the instance transform as
     a float matrix, where Typhoon composes USD's transforms in double, so their
     transformed normals round differently. The planes above still differ by +2.1% at
     roughness 0.04 (the same before and after the port of `Ng` and the second
     normalization). Curved surfaces average the roundings out, as above. A
     renormalized leaf normal, or 1 − NdotH² computed from the half vector's tangential
     components, would remove the dependence.
   - **Typhoon's ray offsets** (the ticket's first suspect): Typhoon's image moved with
     the scene's scale (×30: −0.28%, ×0.03: +0.16% against halcyon), as its fixed 1e-4
     offsets would, but not monotonically; the scale also changes how the transformed
     normals round, which by the above moves these metals by fractions of a percent. The
     bias was the normals': `maxBounces` 0 to 64 leave it at −0.36% from 4 bounces on,
     and a lone convex sphere, whose paths leave after one bounce, showed it too. halcyon's
     scale-relative offsets stay (maintainer decision).
6. **Subsurface in a mix** (found at ticket 47; severity: medium; decided 2026-10-06:
   both kept as Typhoon's, to report upstream). Typhoon divides a
   subsurface sample by the probability of every mix's choice of it, and halcyon keeps
   that. It is right only at the mix whose weight the subsurface leaf repeats as its
   own: OpenPBR and standard_surface give the leaf the weight of the mix that brings it
   in, but their metal mix (and standard_surface's transmission mix) enclose that one
   and divide again, overweighting subsurface by 1 / (1 − metalness) (1 / (1 −
   transmission)); disney_principled's leaf (weight 1, mixed by `subsurface`) and a
   document's `subsurface_bsdf` in a `mix` are overweighted at their own mix too, by
   1 / mix (a white Lambertian mixed half and half with `subsurface_bsdf` reflects 1.5
   by BSDF sampling). Separately, as in Typhoon, a vertex whose BSDF sample is
   subsurface adds no emission or direct light, also for the other lobes of a mixed
   closure: an OpenPBR sphere of white base and subsurface colour, `subsurface_weight`
   0.5, renders 1.0 in an emitting enclosure (BSDF sampling only) but 0.90 under the
   ambient light. sss_randomwalk_gallery shows both, in both renderers alike, so the
   comparison can't see them: its `DisneySssMat` (disney_principled, `subsurface` 0.85)
   overweights subsurface by 1 / 0.85; `StandardSurfaceSssMat` (standard_surface,
   `subsurface` 0.75, metalness and transmission 0, so no enclosing mix) and
   `OpenPbrSssMat` (OpenPBR, `subsurface_weight` 1, so no mix at all) aren't
   overweighted. All three layer a specular lobe over the subsurface (and the first two
   mix in a diffuse lobe), whose direct light is lost where subsurface is chosen.
7. External: the SDK registry's empty volume subtype (above); hdAnari's headless rendering
   and MaterialX path (above).

## The media, subsurface and dispersion comparisons

The acceptance comparisons of pathtracer tickets 17 (interior media), 16 (subsurface
scattering) and 18 (dispersion and caustics), with their later reruns, as AGENTS.md
recorded them until halcyon moved into VisRTX; "the ticket" is the one named here, and
"above" refers to AGENTS.md's section on the feature.

### Interior media (`media.sh`, ticket 17)

**Typhoon comparison** (the ticket's acceptance; the script, `media.sh`, is gone, see
AGENTS.md, "Verification against Typhoon"): it rendered Typhoon's
`volume_transmission_single_cube.usda` (a rough OpenPBR glass cube of scattering interior,
anisotropy 0.45, on a standard_surface ground under the StinsonBeach dome and two sphere
lights) and the same with the cube in three other materials through session layers next to
it (`media_standard_surface`: standard_surface transmission depth and scatter;
`media_absorbing`: OpenPBR glass of an absorbing-only interior; `media_volume`: a MaterialX
volume material, a volume-only boundary) through Typhoon's `usdrender` at seeds 1 and 2,
then halcyon built the same scenes through ANARI (the dome as an hdri as
hdAnari sends it), renders them at the same seeds and sample count and prints each region's
mean (the cube, the ground, the image) per channel against Typhoon's in standard errors, and
the RMS difference per pixel against the noise between Typhoon's seeds; it fails past 4
standard errors and 0.5%, or past 1.1 times that noise. At 256 samples (measured at ticket
17): every region within 0.02% and 0.9 standard errors, the RMS difference across seeds
0.999 to 1.002 times Typhoon's own, and 0.026 (OpenPBR), 0.034 (standard_surface), 0.095
(absorbing) and 0.045 (volume) times it at the same seed: the renderers draw the same
numbers, so their paths agree but where hits round differently. Since ticket 46's
differences (measured there, same Typhoon images): the OpenPBR and standard_surface cubes
render 0.25% and 0.32% brighter in red (1.2 and 2.3 standard errors; the other regions
and the absorbing and volume scenes within 0.03%), as their shadow rays leaving the cube's
surface from inside no longer take the medium's transmittance to the lights (with the
library before ticket 46 the same cubes are within 0.012%); the RMS difference across
seeds 1.003 and 1.005 times Typhoon's own, at the same seed 0.058 and 0.083 times it. The
script still passes; its thresholds are unchanged. `subsurface.sh` and `spectral.sh` are
unchanged by ticket 46 (`spectral.sh`'s regions agree with the library before it to
1e-6). Ticket 48's changes (rerun there, against Typhoon images rendered anew) move only
the standard_surface cube, 0.002% darker in every channel (now +0.317% red, 2.26
standard errors), as next-event estimation from its outer surface through the cube
straight now takes the medium's transmittance; every other region and scene, and
`spectral.sh` and `subsurface.sh`, print the same numbers as before it. Since ticket 49
(shading normals turned into `Ng`'s hemisphere) the three comparisons build their ground
planes (and `spectral.sh`'s receiver planes) wound and normalled as the USD authors them,
against each other, where they used to wind them with their normals or turn the normals
by hand; rerun there against Typhoon images rendered anew, all three print the same
numbers as before.

### Subsurface scattering (`subsurface.sh`, ticket 16)

**Typhoon comparison** (the ticket's acceptance; the script, `subsurface.sh`, is gone, see
AGENTS.md, "Verification against Typhoon"): it rendered Typhoon's
`openpbr_sss_native_adobe_compare.usda` (two cubes of pure OpenPBR subsurface, one with a
specular layer, two sphere lights) and `subsurface_standard_surface.usda`
(the same with the right cube in `sss_randomwalk_gallery.usda`'s standard_surface
subsurface) through Typhoon's `usdrender` at seeds 1 and 2, then
halcyon built the same scenes through ANARI, renders them at the
same seeds and sample count and prints each region's mean (each cube, the ground, the
image) per channel against Typhoon's in standard errors, and the RMS difference per
pixel against the noise between Typhoon's seeds; it fails past 4 standard errors and
0.5%, or past 1.1 times that noise. At 256 samples (measured at ticket 16): every region
within 0.37% and 3.8 standard errors (blue, the shortest mean free path: the ray
offsets above; with Typhoon's fixed offset every region was within 0.5 standard errors),
the RMS difference across seeds 1.002 (openPBR) and 1.000 (standard_surface) times
Typhoon's own, and 0.37 and 0.34 times it at the same seed (the renderers draw the same
numbers; walks diverge where hits round differently). The ANARI scene differs from the
USD where halcyon reads USD differently: its lights aren't `visible` (Typhoon's lights
aren't visible to camera rays unless `visibleInPrimaryRay` says so). Its ground is the
USD's (points that wind it down, normals that point up), which both renderers reconcile
by turning the smooth normal to the winding's side (since ticket 49; before it, the
comparison wound the ground upward).

### Dispersion and caustics (`spectral.sh`, ticket 18)

**Typhoon comparison** (the ticket's acceptance; the script, `spectral.sh`, is gone, see
AGENTS.md, "Verification against Typhoon"): it rendered Typhoon's
`cutglass_dispersion_compare.usda` (a cut-glass mesh in standard_surface glass and in
dispersive OpenPBR glass, Abbe number 10, before two UsdPreviewSurface spheres on a ground
under the StinsonBeach dome, 64 bounces), the same with `enableCaustics`, and its
`caustics_off_policy_regression.usda` with its caustics-off and caustics-on session layers
(thick and thin-walled glass over receivers, smooth and rough glass spheres, an absorbing
glass cube, two sphere lights, a dim dome without a texture) through Typhoon's `usdrender`
at seeds 1 and 2, the scenes' implicit spheres and cubes made Typhoon's own test meshes
(Sphere_Poly, Cube_Poly) by a session layer the script writes, so both renderers trace the
same triangles; halcyon built the same scenes through ANARI (the
textureless dome as the ambient light and a background of its radiance, which sample it
uniformly over the sphere as Typhoon does: an hdri of one texel samples it by its map, and
with caustics off the straight-through shadows make the image depend on that pdf, by up to
0.37% under the transmissive surfaces) and compares each region's mean and the noise as
`media.sh` does, the RMS without each image pair's largest 0.1% of pixel differences: the
sun seen through dispersive glass makes fireflies, more saturated where a path carries one
wavelength, and a handful of pixels decide the plain RMS (across 5 seed pairs it is 0.88
to 1.30 times Typhoon's own, pooled 1.05). At 256 samples (measured at ticket 18): the
cut glass within 0.51% and 1.55 standard errors in every region (the dispersive glass's
blue the farthest), with caustics within 0.49% and 1.46, the RMS difference across seeds
1.004 times Typhoon's own in both, and 0.64 and 0.59 times it at the same seed; the
caustics scene within 0.29% and 1.9 standard errors with caustics off and 0.14% and 1.8
with them on but for the rough glass sphere, 1.3–1.5% darker in both (2.1 standard
errors; 1.0–1.6% over four more seed pairs, and the same at aeca598), the RMS across
seeds 0.974 and 0.991 times Typhoon's, 0.36 and 0.32 at the same seed. The rough sphere
is Typhoon's emitter-only segment (see AGENTS.md, "The quality
renderer", `maxBounces`): its light-only ray mask passes
through the sphere's wall, so rough-glass paths trapped by total internal reflection
until the scene's 16 bounces still find the dome, and Typhoon's image is brighter than
its own converged one (the dome alone: the sphere 0.0076 at 16 bounces in Typhoon, both
renderers about 0.0069 at 64 and 128; at `maxBounces` 0 and 1 Typhoon's is about 25 and
13 times halcyon's; it needs rough transmission: roughness 0.24 and transmission 1 −12%,
transmission 0 none, roughness 0 at transmission 0.75 −0.3%). The script also renders
the scene at 64 bounces on both sides (`caustics_off_64`, `caustics_on_64`, a session
layer and `--max-bounces`): every region, the rough sphere too (−0.25 to −0.33%, 0.4
standard errors), within 0.33% and 1.2 standard errors, the RMS across seeds 0.971 and
0.989 times Typhoon's.
