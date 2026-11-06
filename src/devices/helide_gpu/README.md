# HelideGPU

This is an in-progress implementation of an SDL_gpu device based on libhelium.

On Linux, SDL3 is loaded when the device is initialized rather than when the
device library is loaded. SDL3 development files are still required to build
the device (including generating its Implib.so trampolines). At runtime the
device looks for the SONAME of the SDL3 shared library discovered at build
time. If CMake cannot detect the SONAME, set `SDL3_SONAME` when configuring.
Set `HELIDE_GPU_SDL3_LIBRARY` before device initialization to override the
runtime library path.
