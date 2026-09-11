#include "msaa.h"

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "aim/proto/settings.pb.h"

namespace aim {
namespace {

SDL_GPUSampleCount GetDefaultMsaaSampleCount(SDL_Window* sdl_window, SDL_GPUDevice* gpu_device) {
  int pixel_width = 0;
  int pixel_height = 0;
  if (!SDL_GetWindowSizeInPixels(sdl_window, &pixel_width, &pixel_height)) {
    // Unknown window size. Use conservative 2 samples.
    return SDL_GPU_SAMPLECOUNT_2;
  }
  if (pixel_height < 1200) {
    return SDL_GPU_SAMPLECOUNT_4;
  }
  if (pixel_height < 1500) {
    return SDL_GPU_SAMPLECOUNT_2;
  }
  return SDL_GPU_SAMPLECOUNT_1;
}

static SDL_GPUSampleCount GetMaxSupportedMsaaSampleCount(SDL_Window* sdl_window,
                                                         SDL_GPUDevice* gpu_device) {
  SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(gpu_device, sdl_window);
  for (auto count : {SDL_GPU_SAMPLECOUNT_8, SDL_GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_2}) {
    if (SDL_GPUTextureSupportsSampleCount(gpu_device, format, count)) {
      return count;
    }
  }
  return SDL_GPU_SAMPLECOUNT_1;
}

SDL_GPUSampleCount IntToSampleCount(int count) {
  if (count >= 8) {
    return SDL_GPU_SAMPLECOUNT_8;
  }
  if (count >= 4) {
    return SDL_GPU_SAMPLECOUNT_4;
  }
  if (count >= 2) {
    return SDL_GPU_SAMPLECOUNT_2;
  }
  return SDL_GPU_SAMPLECOUNT_1;
}

MsaaLevel IntToMsaaLevel(int count) {
  if (count >= 8) {
    return MSAA_LEVEL_8X;
  }
  if (count >= 4) {
    return MSAA_LEVEL_4X;
  }
  if (count >= 2) {
    return MSAA_LEVEL_2X;
  }
  return MSAA_LEVEL_OFF;
}

}  // namespace

SDL_GPUSampleCount GetMsaaSampleCount(SDL_Window* sdl_window,
                                      SDL_GPUDevice* gpu_device,
                                      MsaaLevel requested_level) {
  auto get_requested_sample_count = [=] {
    if (requested_level == MSAA_LEVEL_OFF) {
      return SDL_GPU_SAMPLECOUNT_1;
    }
    if (requested_level == MSAA_LEVEL_2X) {
      return SDL_GPU_SAMPLECOUNT_2;
    }
    if (requested_level == MSAA_LEVEL_4X) {
      return SDL_GPU_SAMPLECOUNT_4;
    }
    if (requested_level == MSAA_LEVEL_8X) {
      return SDL_GPU_SAMPLECOUNT_8;
    }
    return GetDefaultMsaaSampleCount(sdl_window, gpu_device);
  };

  SDL_GPUSampleCount requested = get_requested_sample_count();
  SDL_GPUSampleCount max_supported = GetMaxSupportedMsaaSampleCount(sdl_window, gpu_device);
  int final_level = std::min(SampleCountToInt(requested), SampleCountToInt(max_supported));
  return IntToSampleCount(final_level);
}

MsaaLevel SampleCountToMsaaLevel(SDL_GPUSampleCount sample_count) {
  return IntToMsaaLevel(SampleCountToInt(sample_count));
}

int SampleCountToInt(SDL_GPUSampleCount count) {
  switch (count) {
    case SDL_GPU_SAMPLECOUNT_1:
      return 1;
    case SDL_GPU_SAMPLECOUNT_2:
      return 2;
    case SDL_GPU_SAMPLECOUNT_4:
      return 4;
    case SDL_GPU_SAMPLECOUNT_8:
      return 8;
  }
  return 1;
}

}  // namespace aim
