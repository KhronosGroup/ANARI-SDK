# HelideGPU

This is an in-progress implementation of an SDL_gpu device based on libhelium.

SDL3 is dynamically loaded when the device initializes on Linux, macOS, and
Windows. SDL3 development files are still required at build time. The Python
wheel depends on PySDL3 and imports it first; the trampoline looks in PySDL3's
`sdl3/bin` directory for the platform SDL3 library. Native users can set
`HELIDE_GPU_SDL3_LIBRARY` to the library path before device initialization.

## Shading

Surfaces are shaded with the lighting model of OpenUSD Storm's
UsdPreviewSurface (`shaders/shading.glsl`): Lambert diffuse plus a GGX specular
lobe with Schlick Fresnel, in the metallic workflow. `matte` is metallic 0 and
roughness 1; `physicallyBased` takes `metallic` and `roughness` (a value or a
geometry attribute), `ior` and `emissive`.

Lights are `directional`, `point` and `spot` (up to 16 in a frame, counting
instanced ones). The renderer's light (`ambientRadiance` times `ambientColor`,
or white 1 with `ignoreAmbientLighting`) is split by `eyeLightBlendRatio`: that
share drives a headlight, which only lights a world without lights, and the
rest is a uniform ambient fill. Screen-space ambient occlusion darkens only the
ambient fill.
