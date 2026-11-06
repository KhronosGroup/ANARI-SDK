// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "sdl3_gpu_device.h"
#include "ShaderData.h"
#if defined(__linux__)
#include "sdl3_runtime_config.h"
#include "sdl3_trampoline_symbol.h"
#endif
#include <SDL3/SDL.h>
#if defined(__linux__)
#include <dlfcn.h>
#include <cstdlib>
#include <mutex>
#endif

#if defined(__linux__)
extern "C" void HELIDE_GPU_SDL3_SET_HANDLE(void *handle);
#endif

namespace helide_gpu {

#if defined(__linux__)
static std::string loadSDL3()
{
  static std::once_flag loadOnce;
  static std::string loadError;

  std::call_once(loadOnce, [] {
    const char *library = std::getenv("HELIDE_GPU_SDL3_LIBRARY");
    if (!library || !*library)
      library = HELIDE_GPU_SDL3_SONAME;
    void *handle = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
      const char *error = dlerror();
      loadError = error ? error : "dlopen failed without an error message";
      return;
    }

    // Keep SDL loaded for the process lifetime; Implib's trampolines retain
    // pointers into the library after resolving their first function.
    HELIDE_GPU_SDL3_SET_HANDLE(handle);
  });
  return loadError;
}
#endif

bool SDLGPUDevice::ensureVideoSubsystemInitialized()
{
#if defined(__linux__)
  // Implib's generated resolver aborts if dlopen fails. Load SDL first so a
  // missing optional runtime can be reported through ANARI instead.
  const std::string runtimeError = loadSDL3();
  if (!runtimeError.empty()) {
    loadError = runtimeError;
    return false;
  }
#endif

  if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) {
    loadError.clear();
    return true;
  }

  const bool initialized = SDL_InitSubSystem(SDL_INIT_VIDEO);
  if (!initialized)
    loadError = SDL_GetError();
  else
    loadError.clear();
  return initialized;
}

const char *SDLGPUDevice::initializationError() const
{
  return loadError.c_str();
}

void SDLGPUDevice::init()
{
  SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV;
#ifdef HELIDE_GPU_HAS_MSL_SHADERS
  formats |= SDL_GPU_SHADERFORMAT_MSL;
#endif

  device = SDL_CreateGPUDevice(formats, false, NULL);
  if (device)
    shaderFormat = detectShaderFormat(device);
}

void SDLGPUDevice::release()
{
  if (device) {
    SDL_WaitForGPUIdle(device);
    SDL_DestroyGPUDevice(device);
    device = nullptr;
  }
#if 0
  SDL_Quit();
#endif
}

} // namespace helide_gpu
