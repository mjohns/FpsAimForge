#pragma once

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "aim/proto/settings.pb.h"

namespace aim {

SDL_GPUSampleCount GetMsaaSampleCount(SDL_Window* sdl_window,
                                      SDL_GPUDevice* gpu_device,
                                      MsaaLevel requested_level);

MsaaLevel SampleCountToMsaaLevel(SDL_GPUSampleCount);

int SampleCountToInt(SDL_GPUSampleCount count);

}  // namespace aim
