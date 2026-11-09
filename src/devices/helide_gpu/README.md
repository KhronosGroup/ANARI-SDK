# HelideGPU

This is an in-progress implementation of an SDL_gpu device based on libhelium.

SDL3 is dynamically loaded when the device initializes on Linux, macOS, and
Windows. SDL3 development files are still required at build time. The Python
wheel depends on PySDL3 and imports it first; the trampoline looks in PySDL3's
`sdl3/bin` directory for the platform SDL3 library. Native users can set
`HELIDE_GPU_SDL3_LIBRARY` to the library path before device initialization.
