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
roughness 1; `physicallyBased` takes `metallic` and `roughness` (a value or a
geometry attribute), `ior` and `emissive`.

Lights are `directional`, `point`, `spot` and `quad` (up to 16 in a frame,
counting instanced ones). The renderer's light (`ambientRadiance` times
`ambientColor`, or white 1 with `ignoreAmbientLighting`) is split by
`eyeLightBlendRatio`: that share drives a headlight, which only lights a world
without lights, and the rest is a uniform ambient fill. Screen-space ambient occlusion darkens only the
ambient fill.

A `quad` light is shaded the way Storm shades a UsdLux rect light: as a point
light at the quad's center. Unlike Storm, it falls off with the cosine away from
the quad's normal, as a flat Lambertian emitter does, and it honors `side`;
the front faces along `cross(edge2, edge1)`. Its brightness is the first of
`intensity` (W/sr along the normal), `power` (W; a side emits pi times its
intensity) and `radiance` (W/sr/m^2, times the quad's area after the instance
transform). Far from the quad this is accurate; up close it lacks the soft,
quad-shaped light and highlights of a real area light.

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
