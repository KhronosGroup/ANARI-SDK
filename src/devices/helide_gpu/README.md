# HelideGPU

This is an in-progress implementation of an SDL_gpu device based on libhelium.

SDL3 is dynamically loaded when the device initializes on Linux, macOS, and
Windows. SDL3 development files are still required at build time. The Python
wheel depends on PySDL3 and imports it first; the trampoline looks in PySDL3's
`sdl3/bin` directory for the platform SDL3 library. Native users can set
`HELIDE_GPU_SDL3_LIBRARY` to the library path before device initialization.

## Geometry attributes

Triangles take `color` and `attribute0..3` per vertex (`vertex.*`), per
primitive (`primitive.*`) or per triangle corner (`faceVarying.*`, indexed by
`3 * primitive + corner`), and `faceVarying.normal` overrides `vertex.normal`.
When a slot has more than one, faceVarying wins over per-vertex, which wins
over per-primitive. `faceVarying.tangent` is accepted and ignored.

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
headlight, which only lights a world without lights (an `hdri` counts), and
the rest is a uniform ambient fill. Screen-space ambient occlusion darkens only the
ambient fill.

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
