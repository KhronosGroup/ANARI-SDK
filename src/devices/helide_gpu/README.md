# HelideGPU

This is an in-progress implementation of an SDL_gpu device based on libhelium.

SDL3 is loaded at runtime on Linux, macOS, and Windows. SDL3 development files
are still required at build time. The Python wheel depends on PySDL3 and imports
it first; the trampoline uses the SDL3 library loaded by PySDL3, then checks
`SDL3_ROOT` and PySDL3's binary directory. Native users can set
`HELIDE_GPU_SDL3_LIBRARY` to the library path before device initialization.

On macOS, the Python trampoline preloads SDL3 with global symbol visibility
before loading the device library so its dynamic-lookup references can bind.
Native macOS applications must likewise preload SDL3 with `RTLD_GLOBAL` before
loading the ANARI device library; setting the runtime path alone is not enough.

The Python trampoline configures SDL automatically on import. To observe
device status messages, call `helide_gpu.set_status_callback(callback)` before
creating a device; the callback receives the standard ANARI status arguments.

## Geometry attributes

Triangles take `color` and `attribute0..3` per vertex (`vertex.*`), per
primitive (`primitive.*`) or per triangle corner (`faceVarying.*`, indexed by
`3 * primitive + corner`), and `faceVarying.normal` overrides `vertex.normal`.
When a slot has more than one, faceVarying wins over per-vertex, which wins
over per-primitive. `faceVarying.tangent` is accepted and ignored.

## Camera clip planes

As VisGL does, each frame fits the camera's near and far clip planes to the
bounds of what it draws (the world's surfaces and volumes, and its visible quad
lights) projected on the view direction, so the depth buffer's precision isn't
spread over empty space. Without a `near`, a perspective camera's near plane
stays at least 1/1000 of its far, which clips the foreground of a world much
larger than the view; set `near` to see it. A `near` or `far` the application
sets narrows the fitted range (`0 < near < far`, `0 <= near` for an
orthographic camera; invalid values are ignored with a warning). The depth
channel is the depth buffer's, so its values depend on the fitted range.

## Shading

Surfaces are shaded with the lighting model of OpenUSD Storm's
UsdPreviewSurface (`shaders/shading.glsl`): Lambert diffuse plus a GGX specular
lobe with Schlick Fresnel, in the metallic workflow. `matte` is metallic 0 and
roughness 1; `physicallyBased` takes `metallic` and `roughness` (a value, a
geometry attribute or a sampler, whose first output component is used), `ior`
and `emissive` (a value or a sampler). `opacity` on both may be a value or a
sampler (its first output component). `physicallyBased`'s `normal` is a
tangent-space normal map from an `image1D/2D/3D` sampler, its output used as
is (`outTransform`/`outOffset` decode it to [-1,1]). As in Storm, its tangent
frame comes from the screen-space derivatives of the position and of the
sampler's texture coordinate, not from `vertex.tangent`; it maps only
triangles, as a sphere's attributes are constant across it, and the `normal`
frame channel and ambient occlusion keep the unperturbed normal.

Lights are `directional`, `point`, `spot` and `quad` (up to 16 in a frame,
counting instanced ones), and `hdri` (any number). The renderer's light
(`ambientRadiance` times `ambientColor`, or white 1 with
`ignoreAmbientLighting`) is split by `eyeLightBlendRatio`: that share drives a
headlight, which lights every world, with lights or without, and reflects
only diffusely (no highlight or reflection on glossy or metallic surfaces),
and the rest is a uniform ambient fill. Screen-space ambient occlusion darkens only the
ambient fill.

Ambient occlusion is a port of halcyon's `preview` renderer's, with the same
parameters (see its README): `ambientOcclusion`, `ambientOcclusionRadius`,
`ambientOcclusionIntensity`, `ambientOcclusionEstimator` (`"g3d"`, the
default, `"sao"`, or the experimental `"cortao"`, CORTAO's circular occluders,
github.com/theagentd/CORTAO), `ambientOcclusionBlurPasses` (default 2: the
second pass removes most of the blotches `"cortao"` leaves near occluders and
smooths bands near contacts) and `cortaoSamples` (default 16, 1 to 64: more
remove more of those blotches at a higher cost). `"cortao"` is ported from
halcyon's port in `shaders/cortao.glsl`, which carries CORTAO's MIT license;
unlike halcyon it keeps no mip chain of the hit buffer but reads, for a tap
of a coarse level, the pixel that level's texel would hold, so the two give
the same occlusion.

A `quad` light is shaded the way Storm shades a UsdLux rect light: as a point
light at the quad's center. Unlike Storm, it falls off with the cosine away from
the quad's normal, as a flat Lambertian emitter does, and it honors `side`;
the front faces along `cross(edge2, edge1)`. Its brightness is the first of
`intensity` (W/sr along the normal), `power` (W; a side emits pi times its
intensity) and `radiance` (W/sr/m^2, times the quad's area after the instance
transform). Far from the quad this is accurate; up close it lacks the soft,
quad-shaped light and highlights of a real area light.

With `visible` (`KHR_LIGHT_PRIMARY_VISIBILITY`, default true) the camera sees
a quad light as its rectangle, showing the radiance of an emitting side and
nothing of a side that doesn't emit; Storm doesn't draw lights. Surfaces in
front hide it. Where it is seen, the ID channels report no hit, the albedo and
normal channels are 0, and ambient occlusion ignores it. Point, spot and
directional lights have no area to see.

An `hdri` light is treated as halcyon's `preview` renderer treats it, so the
two agree. It doesn't light surfaces directly: its mean radiance over the
sphere (each texel weighed by its solid angle), times `scale` and `color`,
adds to the uniform ambient fill, which Storm's dome-light term then shades
(Storm itself uses prefiltered irradiance and specular maps). Where camera
rays miss, the `visible` hdris (default true) show in place of the
background, summed and opaque, looked up unfiltered (nearest texel), and the
ID, albedo and normal channels report no surface. The map's mapping is
halcyon's and VisRTX's: the image's center column faces `direction` (made
perpendicular to `up`), a quarter turn before it faces `up` x `direction`,
and the array's last row faces `up`. An instance transform turns `up` and
`direction`. Only the `equirectangular` layout exists. With ambient occlusion
the ambient light a surface reflects passes through a buffer that clips it at
4, which a bright hdri can reach.

### Potential improvements: area lights

- **Analytic area light, no lookup tables.** Diffuse light from the exact
  irradiance of a polygon (Lambert's form factor, a sum over the quad's 4
  edges), and specular from the point on the quad nearest the reflection ray
  (Karis' "representative point", *Real Shading in Unreal Engine 4*, 2013).
  It needs the quad's 4 corners in `LightData`. Soft, correctly shaped
  diffuse light up close; specular highlights are approximate.
- **Linearly transformed cosines** (Heitz et al., *Real-Time Polygonal-Light
  Shading with Linearly Transformed Cosines*, 2016). The most accurate for a
  GGX BRDF, but it needs two 64x64 lookup textures bound to both surface
  fragment shaders, which changes their sampler bindings (and the MSL binding
  flags).

# CI notes

PySDL3 downloads its runtime from GitHub on first import. On shared CI runners,
set `SDL_GITHUB_TOKEN` to a read-only GitHub token to avoid unauthenticated API
rate limits. Set `SDL_DOC_GENERATOR=0` to disable its unrelated documentation
downloads and generation.

The Linux wheel validation job renders with Mesa's Lavapipe software Vulkan
driver under Xvfb, so it needs no physical GPU. It sets
`HELIDE_GPU_REQUIRE_GPU=1` to fail rather than skip if no SDL GPU backend can
be created. Without that setting, the wheel test skips when no backend is
available, but still fails if the SDL runtime cannot be loaded.
