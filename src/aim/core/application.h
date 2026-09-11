#pragma once

#include <memory>

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "aim/audio/sound_manager.h"
#include "aim/common/random.h"
#include "aim/common/simple_types.h"
#include "aim/core/application_state.h"
#include "aim/core/file_system.h"
#include "aim/core/font_manager.h"
#include "aim/core/screen.h"
#include "aim/graphics/crosshair_renderer.h"
#include "aim/graphics/textures.h"
#include "spdlog/logger.h"

namespace aim {

class BundleManager;
class ScenarioManager;
class PlaylistManager;
class GuideManager;
class AimDb;
class StatsManager;
class SettingsManager;
class PlayTimeManager;
class Renderer;
class RenderContext;
class ReplayManager;
class LocalStore;
class HistoryManager;
class LabelsManager;

class Application {
 public:
  virtual ~Application() {}

  // Returns whether the program should exit. If it returns false, the application will restart.
  virtual bool RunMainLoop() = 0;

  // Should only be called using methods in Screen
  virtual std::shared_ptr<Screen> PopScreenInternal() = 0;
  virtual void PushScreenInternal(std::shared_ptr<Screen> screen) = 0;

  virtual int GetScreenStackSize() const = 0;
  virtual std::shared_ptr<Screen> GetCurrentScreen() = 0;

  virtual void ReturnToHomeScreen() = 0;
  virtual void PushNextScreen(std::shared_ptr<Screen> next_screen) = 0;
  virtual void PopCurrentScreen() = 0;

  virtual bool BeginFullscreenWindow(const std::string& id = "Fullscreen") = 0;

  virtual SDL_Window* sdl_window() = 0;
  virtual SDL_GPUDevice* gpu_device() = 0;

  virtual bool HasInputFocus() = 0;

  virtual ScreenInfo screen_info() = 0;

  virtual float GetAppRunTimeSeconds() const = 0;

  virtual void EnableVsync() = 0;
  virtual void SetPresentMode(PresentMode present_mode) = 0;
  virtual MsaaLevel GetCurrentMsaaLevel() = 0;

  virtual void RequestExit() = 0;
  virtual void RequestRestart() = 0;

  virtual Random& rand() = 0;
  virtual SoundManager& sound_manager() = 0;
  virtual StatsManager& stats_manager() = 0;
  virtual PlayTimeManager& play_time_manager() = 0;
  virtual Renderer& renderer() = 0;
  virtual FileSystem& file_system() = 0;
  virtual FontManager& font_manager() = 0;
  virtual SettingsManager& settings_manager() = 0;
  virtual ScenarioManager& scenario_manager() = 0;
  virtual PlaylistManager& playlist_manager() = 0;
  virtual GuideManager& guide_manager() = 0;
  virtual BundleManager& bundle_manager() = 0;
  virtual HistoryManager& history_manager() = 0;
  virtual LabelsManager& labels_manager() = 0;
  virtual LocalStore& local_store() = 0;
  virtual CrosshairRenderer& crosshair_renderer() = 0;
  virtual ReplayManager& replay_manager() = 0;
  virtual AimDb& db() = 0;
  virtual spdlog::logger* logger() = 0;
  virtual ApplicationState& state() = 0;
  virtual Texture& logo_texture() = 0;

  virtual std::string GetDebugInfoString() = 0;
};

std::unique_ptr<Application> CreateNewApplication();

}  // namespace aim
