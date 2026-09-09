#include "scenario.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <memory>

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "absl/cleanup/cleanup.h"
#include "absl/strings/ascii.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/name_util.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/camera.h"
#include "aim/core/play_time_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/replay_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/settings_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/graphics/crosshair_renderer.h"
#include "aim/graphics/renderer.h"
#include "aim/proto/common.pb.h"
#include "aim/proto/settings.pb.h"
#include "aim/scenario/metronome.h"
#include "aim/scenario/replay_viewer.h"
#include "aim/scenario/scenario_factory.h"
#include "aim/scenario/scenario_timer.h"
#include "aim/ui/editor/scenario_editor_screen.h"
#include "aim/ui/quick_settings_screen.h"
#include "aim/ui/stats/stats_screen.h"
#include "aim/ui/ui_app.h"
#include "aim/ui/ui_screen.h"
#include "glm/mat4x4.hpp"  // IWYU pragma: keep
#include "glm/vec2.hpp"    // IWYU pragma: keep
#include "glm/vec3.hpp"    // IWYU pragma: keep
#include "imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"

namespace aim {
namespace {

constexpr const i16 kReplayFps = 240;
constexpr const i16 kStaticReplayFps = 480;
constexpr const int kDefaultTargetRenderFps = 600;
constexpr const i64 kClickDebounceMicros = 3 * 1000;

bool RequiresPerFrameTargetData(const ScenarioDef& def) {
  // Does the scenario require position, radius, health to change after the target has been added?

  if (!def.has_static_def()) {
    // Only static doesn't need target positions that update over time.
    return true;
  }

  bool has_health = def.shot_type().type_case() == ShotType::kTrackingKill ||
                    def.shot_type().type_case() == ShotType::kClickMulti;
  if (has_health) {
    return true;
  }

  // See if the radius of the targets changes over time.
  for (const TargetProfile& p : def.target_def().profiles()) {
    if (p.target_radius_growth_time_seconds() > 0) {
      return true;
    }
    if (p.target_radius_at_kill() > 0) {
      return true;
    }
  }

  return false;
}

bool ShouldRecordReplay(ScenarioDef& def,
                        bool requires_per_frame_target_data,
                        const Settings& settings) {
  if (settings.disable_replays()) {
    return false;
  }
  if (def.duration_seconds() > 80 || def.target_def().num_targets() <= 0) {
    return false;
  }
  if (requires_per_frame_target_data) {
    return def.target_def().num_targets() < 20;
  }
  return true;
}

}  // namespace

Scenario::~Scenario() {
  FlushPlayTime();
}

void Scenario::FlushPlayTime() {
  if (play_time_flushed_) {
    return;
  }
  play_time_flushed_ = true;
  float elapsed = timer_.GetElapsedSeconds();
  if (elapsed > 0) {
    PlayTimeDetails details;
    details.is_complete_run = is_done();
    details.shot_type = GetShotType();
    details.cm_per_360 = effective_cm_per_360_;
    app_.play_time_manager().AddPlayTime(scenario_name_, elapsed, details);
  }
}

Scenario::Scenario(const CreateScenarioParams& params)
    : Screen(GetUiApp()),
      scenario_name_(params.name),
      def_(params.def),
      timer_(kReplayFps),
      camera_(Camera(CameraParams(params.def.room()))),
      target_manager_(params.def.room()),
      force_start_immediately_(params.force_start_immediately),
      from_scenario_editor_(params.from_scenario_editor),
      create_params_(params) {
  theme_ = app_.settings_manager().GetCurrentTheme();
  settings_ = app_.settings_manager().GetCurrentSettingsForScenario(scenario_name_);

  bool requires_per_frame_target_data = RequiresPerFrameTargetData(def_);
  u16 replay_fps = requires_per_frame_target_data ? kReplayFps : kStaticReplayFps;
  timer_ = ScenarioTimer(replay_fps);
  if (!from_scenario_editor_ &&
      ShouldRecordReplay(def_, requires_per_frame_target_data, settings_)) {
    replay_ = std::make_unique<ReplayRecorder>(scenario_name_,
                                               def_.room(),
                                               def_.shot_type().type_case(),
                                               replay_fps,
                                               static_cast<i32>(def_.duration_seconds()),
                                               def_.target_def().num_targets(),
                                               requires_per_frame_target_data);
  }
}

void Scenario::RefreshState() {
  settings_ = app_.settings_manager().GetCurrentSettingsForScenario(scenario_name_);
  app_.sound_manager().LoadSounds(settings_);
  float render_fps = FirstGreaterThanZero(settings_.max_render_fps(), kDefaultTargetRenderFps);
  max_render_age_micros_ = (1 / (float)(render_fps + 1)) * 1000 * 1000;
  projection_ = GetPerspectiveTransformation(app_.screen_info(), def_.room().horizontal_fov());

  float dpi = app_.settings_manager().GetDpi();
  metronome_ =
      std::make_unique<Metronome>(settings_.enable_metronome() ? settings_.metronome_bpm() : 0,
                                  settings_.sounds().metronome(),
                                  &app_);

  effective_cm_per_360_ = settings_.cm_per_360();

  NameInfo name_info = GetScenarioNameInfo(scenario_name_);
  if (name_info.cm_per_360) {
    effective_cm_per_360_ = *name_info.cm_per_360;
  }

  if (effective_cm_per_360_ <= 0) {
    effective_cm_per_360_ = 0.1;
  }
  radians_per_dot_ = CmPer360ToRadiansPerDot(effective_cm_per_360_, dpi);

  is_click_held_ = false;
  crosshair_ = app_.settings_manager().GetCurrentCrosshair();
  crosshair_size_ = settings_.crosshair_size();
  theme_ = app_.settings_manager().GetCurrentTheme();

  {
    auto& h = theme_.health_bar();
    auto health_rgb = ToStoredRgb(h.health_color());
    auto background_rgb = ToStoredRgb(h.background_color());
    health_color_ = IM_COL32(health_rgb.r(),
                             health_rgb.g(),
                             health_rgb.b(),
                             (h.has_health_alpha() ? h.health_alpha() : 1.0f) * 255);
    health_background_color_ =
        IM_COL32(background_rgb.r(),
                 background_rgb.g(),
                 background_rgb.b(),
                 (h.has_background_alpha() ? h.background_alpha() : 1.0f) * 255);
  }
}

void Scenario::OnEvents(std::span<SDL_Event> events) {
  current_times_.events_count = events.size();
  float xrel = 0;
  float yrel = 0;
  bool has_mouse_move = false;
  for (const SDL_Event& event : events) {
    if (event.type == SDL_EVENT_MOUSE_MOTION && (is_running() || is_start_countdown())) {
      current_times_.mouse_events_count++;
      xrel += event.motion.xrel;
      yrel += event.motion.yrel;
      has_mouse_move = true;
    }
    OnEvent(event);
  }
  if (has_mouse_move) {
    camera_.Update(xrel, yrel, radians_per_dot_);
  }
}

void Scenario::OnEvent(const SDL_Event& event) {
  if (IsQuitEvent(event)) {
    app_.RequestExit();
  }
  if (is_adjusting_crosshair_) {
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
      if (event.wheel.y != 0) {
        crosshair_size_ += event.wheel.y;
      }
    }
  }

  if (IsMappableKeyDownEvent(event)) {
    std::string event_name = absl::AsciiStrToLower(GetKeyNameForEvent(event));

    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().fire())) {
      is_click_held_ = true;
      if (is_running()) {
        if (KeyMappingMatchesEvent(event_name, settings_.keybinds().fire())) {
          i64 now_micros = timer_.GetElapsedMicros();
          if (now_micros - last_click_time_micros_ > kClickDebounceMicros) {
            update_data_.has_click = true;
            last_click_time_micros_ = now_micros;
          }
        }
      } else if (is_waiting_for_click_to_start()) {
        update_data_.has_click = true;
      }
    }

    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().edit_scenario())) {
      if (from_scenario_editor_) {
        PopSelf();
      } else {
        ReturnHome();
        ScenarioEditorOptions opts;
        opts.scenario_name = scenario_name_;
        PushNextScreen(CreateScenarioEditorScreen(opts));
      }
    }
    if (!is_waiting_for_click_to_start() &&
        KeyMappingMatchesEvent(event_name, settings_.keybinds().restart_scenario())) {
      PopSelf();
      // In scenario editing make the restart kebind just return to the editor.
      if (!from_scenario_editor_) {
        std::shared_ptr<Screen> restart_running_scenario = CreateScenario(create_params_);
        if (restart_running_scenario) {
          app_.scenario_manager().SetCurrentRunningScenario(restart_running_scenario);
          PushNextScreen(restart_running_scenario);
        }
      }
    }
    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().quick_settings())) {
      PushNextScreen(
          CreateQuickSettingsScreen(scenario_name_, QuickSettingsType::DEFAULT, event_name));
    }
    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().quick_metronome())) {
      PushNextScreen(
          CreateQuickSettingsScreen(scenario_name_, QuickSettingsType::METRONOME, event_name));
    }
    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().adjust_crosshair_size())) {
      is_adjusting_crosshair_ = true;
    }
  }
  if (IsEscapeKeyDown(event)) {
    PopSelf();
  }
  if (IsMappableKeyUpEvent(event)) {
    std::string event_name = absl::AsciiStrToLower(GetKeyNameForEvent(event));
    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().adjust_crosshair_size())) {
      is_adjusting_crosshair_ = false;
      save_crosshair_ = true;
    }
    if (KeyMappingMatchesEvent(event_name, settings_.keybinds().fire())) {
      is_click_held_ = false;
    }
  }

  if (is_running()) {
    OnScenarioEvent(event);
  }
}

void Scenario::BeginRunWithOptionalCountdown() {
  float countdown_seconds = settings_.start_countdown_time();
  if (countdown_seconds > 0) {
    start_countdown_stopwatch_.Start();
    run_state_ = ScenarioRunState::START_COUNTDOWN;
    return;
  }
  run_state_ = ScenarioRunState::RUNNING;
  timer_.ResumeRun();
}

void Scenario::OnAttach() {
  app_.SetPresentMode(settings_.present_mode());
  SDL_SetWindowRelativeMouseMode(app_.sdl_window(), true);
  RefreshState();
  timer_.StartLoop();
  if (is_running()) {
    timer_.ResumeRun();
  }
}

void Scenario::OnDetach() {
  timer_.PauseRun();
  OnPause();
}

void Scenario::OnTickStart() {
  update_data_ = {};
  if (run_state_ == ScenarioRunState::DONE) {
    // TODO: Maybe should clear in ApplicationState
    PopSelf();
    return;
  }
  if (!app_.HasInputFocus()) {
    PopSelf();
    return;
  }
  if (timer_.GetElapsedSeconds() >= def_.duration_seconds()) {
    HandleScenarioDone();
    return;
  }
  if (save_crosshair_) {
    DoneAdjustingCrosshairSize();
    save_crosshair_ = false;
  }

  if (run_state_ == ScenarioRunState::NOT_STARTED) {
    if (force_start_immediately_) {
      run_state_ = ScenarioRunState::RUNNING;
      timer_.ResumeRun();
    } else if (settings_.disable_click_to_start()) {
      BeginRunWithOptionalCountdown();
    } else {
      run_state_ = ScenarioRunState::WAITING_FOR_CLICK_TO_START;
    }
  }

  if (is_running()) {
    if (!initialized_) {
      Initialize();
      initialized_ = true;
    }
    current_times_ = {};
    current_times_.start = timer_.GetElapsedMicros();
    current_times_.events_start = current_times_.start;
  }
}

void Scenario::OnTick() {
  switch (run_state_) {
    case ScenarioRunState::RUNNING:
      OnRunningTick();
      return;
    case ScenarioRunState::START_COUNTDOWN:
      OnStartCountdownClickTick();
      return;
    case ScenarioRunState::WAITING_FOR_CLICK_TO_START:
      OnWaitingForClickTick();
      return;
    case ScenarioRunState::NOT_STARTED:
    case ScenarioRunState::DONE:
      break;
  }
}

void Scenario::OnWaitingForClickTick() {
  if (update_data_.has_click) {
    BeginRunWithOptionalCountdown();
    return;
  }
  look_at_ = camera_.GetLookAt();
  timer_.OnStartFrame();
  bool do_render = timer_.LastFrameRenderStartedMicrosAgo() > max_render_age_micros_;
  if (!do_render) {
    return;
  }
  timer_.OnStartRender();
  auto end_render_guard = absl::MakeCleanup([&] { timer_.OnEndRender(); });

  ImGui::NewSdlFrame();
  app_.BeginFullscreenWindow();
  app_.crosshair_renderer().Draw(crosshair_, crosshair_size_, theme_, app_.screen_info().center);

  ImGui::PushStyleColor(ImGuiCol_Text, ToImCol32(theme_.target_color()));

  ImGui::Text("%s", scenario_name_.c_str());
  ImGui::Text("fps: %d", (int)ImGui::GetIO().Framerate);
  if (settings_.enable_metronome() && settings_.metronome_bpm() > 0) {
    ImGui::Text("bpm: %.0f", settings_.metronome_bpm());
  }
  ImGui::Text("theme: %s", settings_.theme_name().c_str());
  ImGui::Text("cm/360: %.0f", effective_cm_per_360_);

  {
    auto bold = app_.font_manager().UseLargeBold();
    std::string message = "Click to Start";
    ImVec2 text_size = ImGui::CalcTextSize(message.c_str());
    ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
    ImGui::SetCursorPosY(app_.screen_info().center.y - text_size.y * 1.75);
    ImGui::Text(message);
  }

  ImVec2 text_size = ImGui::CalcTextSize(scenario_name_.c_str());
  ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
  ImGui::SetCursorPosY(app_.screen_info().center.y + text_size.y * 1);
  ImGui::Text("%s", scenario_name_.c_str());

  // TODO: This target score should factor in playlist override.
  float score_target = def_.score_targets().target_score();
  if (score_target > 0) {
    std::string message = std::format("Target score: {}", MaybeIntToString(score_target, 2));
    text_size = ImGui::CalcTextSize(message.c_str());
    ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
    ImGui::Text("%s", message.c_str());
  }

  std::string message = std::format("cm/360: {}", MaybeIntToString(effective_cm_per_360_, 1));
  text_size = ImGui::CalcTextSize(message.c_str());
  ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
  ImGui::Text("%s", message.c_str());

  ImGui::PopStyleColor();
  ImGui::End();

  app_.renderer().RenderScenario(projection_,
                                 def_.room(),
                                 def_.shot_type().type_case(),
                                 theme_,
                                 settings_.health_bar(),
                                 target_manager_.GetTargets(),
                                 look_at_);
}

void Scenario::OnStartCountdownClickTick() {
  float elapsed_seconds = start_countdown_stopwatch_.GetElapsedSeconds();
  float wait_seconds = settings_.start_countdown_time();
  if (wait_seconds <= 0 || elapsed_seconds >= wait_seconds) {
    run_state_ = ScenarioRunState::RUNNING;
    timer_.ResumeRun();
    start_countdown_stopwatch_.Stop();
    return;
  }

  look_at_ = camera_.GetLookAt();
  timer_.OnStartFrame();
  bool do_render = timer_.LastFrameRenderStartedMicrosAgo() > max_render_age_micros_;
  if (!do_render) {
    return;
  }
  timer_.OnStartRender();
  auto end_render_guard = absl::MakeCleanup([&] { timer_.OnEndRender(); });

  ImGui::NewSdlFrame();
  app_.BeginFullscreenWindow();
  app_.crosshair_renderer().Draw(crosshair_, crosshair_size_, theme_, app_.screen_info().center);

  ImGui::PushStyleColor(ImGuiCol_Text, ToImCol32(theme_.target_color()));
  ImGui::Text("%s", scenario_name_.c_str());
  ImGui::Text("fps: %d", (int)ImGui::GetIO().Framerate);
  ImGui::PopStyleColor();

  {
    // Draw the countdown progress bar.
    float time_remaining_percent = (wait_seconds - elapsed_seconds) / wait_seconds;

    ScreenInfo screen_info = app_.screen_info();

    float char_x = ImGui::GetDefaultCharSizeX();
    float width = char_x * 20;

    float left_width = width * time_remaining_percent;

    float center_y = screen_info.height / 2.0f;
    float y_min = center_y - char_x * 14;
    float y_max = y_min + char_x * 2;

    ImVec2 top_left(0, y_min);
    ImVec2 bottom_right(width, y_max);

    ImVec2 top_mid(left_width, y_min);
    ImVec2 bottom_mid(left_width, y_max);

    float x_offset = (screen_info.width - width) / 2.0f;

    top_left.x += x_offset;
    bottom_right.x += x_offset;
    top_mid.x += x_offset;
    bottom_mid.x += x_offset;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(top_left, bottom_mid, GetHealthColor());
    draw_list->AddRectFilled(top_mid, bottom_right, GetHealthBackgroundColor());
  }

  ImGui::End();

  // Draw progress bar.
  app_.renderer().RenderScenario(projection_,
                                 def_.room(),
                                 def_.shot_type().type_case(),
                                 theme_,
                                 settings_.health_bar(),
                                 target_manager_.GetTargets(),
                                 look_at_);
}

void Scenario::OnRunningTick() {
  current_times_.events_end = timer_.GetElapsedMicros();

  loop_count_++;
  if (loop_count_ % 50000 == 0) {
    state_updates_per_second_ = (num_state_updates_ / timer_.GetElapsedSeconds()) / 1000.0;
  }
  // timer_.ResumeRun();

  // TODO: Should this be in OnTickStart so that it happens before event processing?
  timer_.OnStartFrame();
  current_times_.frame_number = loop_count_;

  if (timer_.IsNewReplayFrame()) {
    // Store the look at vector before the mouse updates for the old frame.
    if (replay_) {
      i64 replay_frame_number = timer_.GetReplayFrameNumber();
      replay_->SetPitchYaw(replay_frame_number, camera_.GetPitch(), camera_.GetYaw());
      replay_->SnapshotTargets(replay_frame_number, target_manager_.GetTargets());
    }
  }

  // Update state
  current_times_.update_start = timer_.GetElapsedMicros();
  if (metronome_) {
    metronome_->DoTick(timer_.GetElapsedMicros());
  }
  look_at_ = camera_.GetLookAt();

  update_data_.is_click_held = is_click_held_ || settings_.auto_hold_tracking();
  for (auto& task : delayed_tasks_) {
    if (task.fn.has_value() && task.run_time_seconds < timer_.GetElapsedSeconds()) {
      std::function<void()> fn = std::move(*task.fn);
      task.fn = {};
      fn();
    }
  }
  UpdateState(&update_data_);
  num_state_updates_++;
  current_times_.update_end = timer_.GetElapsedMicros();

  // Render if forced or if the last render was over ~1ms ago.
  bool do_render = update_data_.force_render ||
                   timer_.LastFrameRenderStartedMicrosAgo() > max_render_age_micros_;
  if (!do_render) {
    current_times_.render.start = 0;
    current_times_.render.end = 0;
    UpdatePerfStats();
    return;
  }

  timer_.OnStartRender();
  current_times_.render.start = timer_.GetElapsedMicros();
  auto end_render_guard = absl::MakeCleanup([&] { timer_.OnEndRender(); });

  ImGui::NewSdlFrame();
  app_.BeginFullscreenWindow();

  current_times_.draw_crosshair = timer_.GetElapsedMicros();
  app_.crosshair_renderer().Draw(crosshair_, crosshair_size_, theme_, app_.screen_info().center);

  DrawAdditionalUiElements();

  ImGui::PushStyleColor(ImGuiCol_Text, ToImCol32(theme_.target_color()));
  float elapsed_seconds = timer_.GetElapsedSeconds();
  ImGui::Text("time: %.1f", elapsed_seconds);
  ImGui::Text("fps: %d", (int)ImGui::GetIO().Framerate);
  ImGui::Text("ups: %.1fk", state_updates_per_second_);
  ImGui::Text("cm/360: %.0f", effective_cm_per_360_);
  if (settings_.enable_metronome() && settings_.metronome_bpm() > 0) {
    ImGui::Text("bpm: %.0f", settings_.metronome_bpm());
  }
  ImGui::PopStyleColor();

  ImGui::End();

  RenderContext ctx;
  ctx.stopwatch = &timer_.run_stopwatch();
  ctx.times = &current_times_;
  app_.renderer().RenderScenario(projection_,
                                 def_.room(),
                                 def_.shot_type().type_case(),
                                 theme_,
                                 settings_.health_bar(),
                                 target_manager_.GetTargets(),
                                 look_at_,
                                 &ctx);

  current_times_.render.end = timer_.GetElapsedMicros();
  UpdatePerfStats();
}

void Scenario::HandleScenarioDone() {
  if (from_scenario_editor_) {
    PopSelf();
    return;
  }

  run_state_ = ScenarioRunState::DONE;
  OnScenarioDone();

  std::shared_ptr<PlaylistRun> playlist_run = app_.playlist_manager().GetCurrentRun();
  if (playlist_run) {
    playlist_run->IncrementRunDone(scenario_name_);
  }

  FlushPlayTime();

  PopSelf();

  std::optional<StatsDbRow> maybe_stats_row = GetStatsRow();
  if (maybe_stats_row) {
    StatsDbRow stats_row = *maybe_stats_row;
    app_.stats_manager().AddStats(scenario_name_, &stats_row);
    if (replay_) {
      replay_->FillInMissingPitchYaws();
      assert(stats_row.stats_id > 0 && "Missing stats id. Make sure it was added to db already.");
      app_.replay_manager().AddReplay(stats_row.stats_id, replay_->replay());
    }
    state_.AddPerformanceStats(scenario_name_, stats_row.stats_id, perf_stats_);
    PushNextScreen(CreateStatsScreen(scenario_name_, stats_row.stats_id, true));
  }
}

ShotType::TypeCase Scenario::GetShotType() {
  ShotType::TypeCase shot_type = def_.shot_type().type_case();
  if (shot_type == ShotType::TYPE_NOT_SET) {
    shot_type = GetDefaultShotType();
  }
  return shot_type;
}

void Scenario::UpdatePerfStats() {
  current_times_.end = timer_.GetElapsedMicros();
  current_times_.total = current_times_.end - current_times_.start;

  perf_stats_.total_time_histogram.Increment(current_times_.total);
  perf_stats_.render_time_histogram.Increment(current_times_.render.end -
                                              current_times_.render.start);
  perf_stats_.update_time_histogram.Increment(current_times_.update_end -
                                              current_times_.update_start);
  perf_stats_.events_time_histogram.Increment(current_times_.events_end -
                                              current_times_.events_start);
  if (current_times_.total > perf_stats_.worst_times.total) {
    perf_stats_.worst_times = current_times_;
    perf_stats_.worst_times_micros = timer_.GetElapsedMicros();
  }
  perf_stats_.top_events_count =
      std::max(current_times_.events_count, perf_stats_.top_events_count);
}

void Scenario::DoneAdjustingCrosshairSize() {
  Settings* current_settings = app_.settings_manager().GetMutableCurrentSettings();
  if (current_settings != nullptr) {
    if (crosshair_size_ != current_settings->crosshair_size()) {
      current_settings->set_crosshair_size(crosshair_size_);
      app_.settings_manager().MarkDirty();
      app_.settings_manager().MaybeFlushToDisk(scenario_name_);
      RefreshState();
    }
  }
}

void Scenario::AddNewTargetEvent(const Target& target) {
  if (replay_) {
    replay_->AddTarget(timer_.GetElapsedMicros(), target);
  }
}

void Scenario::AddRemoveTargetEvent(u16 target_id) {
  if (replay_) {
    replay_->RemoveTarget(timer_.GetElapsedMicros(), target_id);
  }
}

void Scenario::PlaySound(SoundType type) {
  app_.sound_manager().PlayLoadedSound(settings_.sounds(), type);
  if (replay_) {
    replay_->PlaySound(timer_.GetElapsedMicros(), type);
  }
}

TargetProfile Scenario::GetNextTargetProfile() {
  return target_manager_.GetTargetProfile(def_.target_def(), app_.rand());
}

Target Scenario::GetTargetTemplate(const TargetProfile& profile) {
  Target target;
  target.radius = app_.rand().GetJittered(profile.target_radius(), profile.target_radius_jitter());
  if (profile.target_radius_at_kill() > 0) {
    RadiusAtKill k;
    k.start_radius = target.radius;
    k.end_radius = profile.target_radius_at_kill();
    target.radius_at_kill = k;
  }
  if (profile.target_radius_growth_time_seconds() > 0) {
    TargetGrowthInfo growth_info;
    growth_info.start_time_seconds = timer_.GetElapsedSeconds();
    growth_info.grow_time_seconds = profile.target_radius_growth_time_seconds();
    growth_info.time_at_final_size_seconds = profile.target_radius_growth_final_size_time_seconds();
    growth_info.end_radius = profile.target_radius_growth_size();
    growth_info.start_radius = target.radius;
    target.growth_info = growth_info;
  }

  target.speed = app_.rand().GetJittered(profile.speed(), profile.speed_jitter());
  target.acceleration =
      app_.rand().GetJittered(profile.acceleration(), profile.acceleration_jitter());
  target.health_seconds = def_.shot_type().health_seconds();
  if (def_.shot_type().has_health_clicks()) {
    target.health_clicks = def_.shot_type().health_clicks();
  }
  if (profile.has_pill()) {
    target.is_pill = true;
    target.height = profile.pill().height();
  }
  target.health_regen_rate = def_.shot_type().health_regen_rate();
  return target;
}

void Scenario::RunAfterSeconds(float delay_seconds, std::function<void()>&& fn) {
  for (auto& task : delayed_tasks_) {
    if (!task.fn.has_value()) {
      task.fn = std::move(fn);
      task.run_time_seconds = timer_.GetElapsedSeconds() + delay_seconds;
      return;
    }
  }

  delayed_tasks_.push_back({});
  DelayedTask& task = delayed_tasks_.back();
  task.fn = std::move(fn);
  task.run_time_seconds = timer_.GetElapsedSeconds() + delay_seconds;
}

}  // namespace aim
