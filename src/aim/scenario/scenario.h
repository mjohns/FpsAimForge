#pragma once

#include <functional>
#include <optional>

#include "aim/audio/sound_manager.h"
#include "aim/core/camera.h"
#include "aim/core/perf.h"
#include "aim/core/screen.h"
#include "aim/core/target.h"
#include "aim/database/aim_db.h"
#include "aim/proto/crosshair.pb.h"
#include "aim/proto/scenario.pb.h"
#include "aim/proto/settings.pb.h"
#include "aim/proto/theme.pb.h"
#include "aim/scenario/metronome.h"
#include "aim/scenario/replay.h"
#include "aim/scenario/scenario_timer.h"
#include "glm/mat4x4.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"    // IWYU pragma: keep

namespace aim {

enum ScenarioRunState {
  NOT_STARTED,
  WAITING_FOR_CLICK_TO_START,
  START_COUNTDOWN,
  RUNNING,
  DONE,
};

struct CreateScenarioParams {
  std::string name;
  ScenarioDef def;
  bool force_start_immediately = false;
  bool from_scenario_editor = false;
};

struct UpdateStateData {
  bool has_click = false;
  bool is_click_held = false;
  // Set to true to force rendering.
  bool force_render = false;
};

struct DelayedTask {
  std::optional<std::function<void()>> fn;
  float run_time_seconds;
};

class Scenario : public Screen {
 public:
  Scenario(const CreateScenarioParams& params);
  virtual ~Scenario();

  bool is_done() const {
    return run_state_ == ScenarioRunState::DONE;
  }

  bool is_running() {
    return run_state_ == ScenarioRunState::RUNNING;
  }

  bool is_waiting_for_click_to_start() {
    return run_state_ == ScenarioRunState::WAITING_FOR_CLICK_TO_START;
  }

  bool is_start_countdown() {
    return run_state_ == ScenarioRunState::START_COUNTDOWN;
  }

  void OnEvents(std::span<SDL_Event> events) override;

  void OnTick() override;
  void OnTickStart() override;

 protected:
  void OnAttach() override;
  void OnDetach() override;

  virtual void Initialize() {}
  virtual void OnScenarioEvent(const SDL_Event& event) {}
  virtual void UpdateState(UpdateStateData* data) = 0;
  virtual void OnScenarioDone() {}
  virtual void OnPause() {}

  virtual void DrawAdditionalUiElements() {}

  virtual std::optional<StatsDbRow> GetStatsRow() {
    return {};
  }

  virtual ShotType::TypeCase GetDefaultShotType() {
    return ShotType::kClickSingle;
  }

  ShotType::TypeCase GetShotType();

  void RunAfterSeconds(float delay_seconds, std::function<void()>&& fn);

  // Replay recording methods
  void AddNewTargetEvent(const Target& target);
  void AddRemoveTargetEvent(u16 target_id);

  void PlaySound(SoundType type);

  TargetProfile GetNextTargetProfile();
  Target GetTargetTemplate(const TargetProfile& profile);

  ImU32 GetHealthColor() {
    return health_color_;
  }

  ImU32 GetHealthBackgroundColor() {
    return health_background_color_;
  }

  std::string scenario_name_;
  ScenarioDef def_;
  std::unique_ptr<Metronome> metronome_;
  ScenarioTimer timer_;
  Camera camera_;
  TargetManager target_manager_;
  LookAtInfo look_at_;
  glm::mat4 projection_;

  std::unique_ptr<ReplayRecorder> replay_;
  Theme theme_;
  bool has_started_ = false;
  ScenarioRunState run_state_ = ScenarioRunState::NOT_STARTED;
  Settings settings_;
  float effective_cm_per_360_ = 0;

 private:
  void OnEvent(const SDL_Event& event);
  void OnRunningTick();
  void OnWaitingForClickTick();
  void OnStartCountdownClickTick();

  void BeginRunWithOptionalCountdown();

  void RefreshState();

  void DoneAdjustingCrosshairSize();
  void DoneAdjustingHealthBarSize();

  void UpdatePerfStats();
  void HandleScenarioDone();

  void FlushPlayTime();

  i64 num_state_updates_ = 0;
  float state_updates_per_second_ = 0;
  float radians_per_dot_;
  Crosshair crosshair_;
  float crosshair_size_;
  bool is_click_held_ = false;
  bool is_done_ = false;
  std::vector<DelayedTask> delayed_tasks_;
  i64 max_render_age_micros_;

  Stopwatch start_countdown_stopwatch_;

  FrameTimes current_times_;
  RunPerformanceStats perf_stats_;
  bool force_start_immediately_ = false;
  bool is_adjusting_crosshair_ = false;
  bool is_adjusting_health_bar_size_ = false;
  bool save_crosshair_ = false;
  bool save_health_bar_ = false;
  bool initialized_ = false;
  i64 last_click_time_micros_ = 0;
  UpdateStateData update_data_;
  i64 loop_count_ = 0;
  bool from_scenario_editor_;
  bool play_time_flushed_ = false;
  const CreateScenarioParams create_params_;
  ImU32 health_color_;
  ImU32 health_background_color_;
  i64 waiting_start_time_micros_ = -1;
};

}  // namespace aim
