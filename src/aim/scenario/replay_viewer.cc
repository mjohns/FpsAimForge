#include "replay_viewer.h"

#include <algorithm>

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "absl/cleanup/cleanup.h"
#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/local_store.h"
#include "aim/core/settings_manager.h"
#include "aim/graphics/crosshair_renderer.h"
#include "aim/graphics/renderer.h"
#include "aim/scenario/replay.h"
#include "aim/scenario/scenario_timer.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "implot.h"

namespace aim {
namespace {

i64 GetMicrosPerFrame(u16 replay_fps) {
  return SecondsToMicros(1 / static_cast<float>(replay_fps));
}

constexpr const char* kPlaybackSpeedKey = "ReplayPlaybackSpeed";

enum class PlaybackSpeed : int {
  SPEED_100 = 100,
  SPEED_75 = 75,
  SPEED_50 = 50,
  SPEED_40 = 40,
  SPEED_30 = 30,
  SPEED_20 = 20,
  SPEED_10 = 10,
};

const std::vector<std::pair<PlaybackSpeed, std::string>> kPlaybackSpeeds{
    {PlaybackSpeed::SPEED_100, "1.0"},
    {PlaybackSpeed::SPEED_75, "0.75"},
    {PlaybackSpeed::SPEED_50, "0.50"},
    {PlaybackSpeed::SPEED_40, "0.40"},
    {PlaybackSpeed::SPEED_30, "0.30"},
    {PlaybackSpeed::SPEED_20, "0.20"},
    {PlaybackSpeed::SPEED_10, "0.10"},
};

std::vector<float> GetMouseSpeeds(const Replay& replay) {
  std::vector<float> result;
  result.reserve(replay.pitch_yaws.size());

  double seconds_per_frame = 1.0 / static_cast<double>(replay.replay_fps);

  // When calculating delta, look at the pitch/yaw n frames ahead.
  i64 look_ahead_size = 4;
  float last_speed = 0;
  for (int frame_number = 0; frame_number < replay.pitch_yaws.size() - look_ahead_size;
       ++frame_number) {
    const PitchYaw& current_pitch_yaw = replay.pitch_yaws[frame_number];
    const PitchYaw& next_pitch_yaw = replay.pitch_yaws[frame_number + look_ahead_size];

    float delta_pitch = next_pitch_yaw.pitch - current_pitch_yaw.pitch;
    float delta_yaw = next_pitch_yaw.yaw - current_pitch_yaw.yaw;

    float delta = abs(delta_pitch) + abs(delta_yaw);

    float delta_per_second = delta / (seconds_per_frame * look_ahead_size);

    float speed = delta_per_second * 100;
    result.push_back(speed);
    last_speed = speed;
  }
  return result;
}

struct MouseSpeedPlotData {
  std::span<float> mouse_speeds;
  float t_step;
};

void DrawMouseSpeedsPlot(float now, float t_step, std::span<float> mouse_speeds, float max_speed) {
  if (mouse_speeds.empty()) {
    return;
  }

  MouseSpeedPlotData mouse_data;
  mouse_data.mouse_speeds = mouse_speeds;
  mouse_data.t_step = t_step;

  auto point_getter = [](int idx, void* data_ptr) {
    MouseSpeedPlotData data = *((MouseSpeedPlotData*)data_ptr);
    return ImPlotPoint(idx * data.t_step, data.mouse_speeds[idx]);
  };

  ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
  float char_x = ImGui::GetDefaultCharSizeX();
  ImPlotFlags plot_flags =
      ImPlotFlags_NoFrame | ImPlotFlags_NoLegend | ImPlotFlags_NoTitle | ImPlotFlags_None;
  if (!ImPlot::BeginPlot("MouseSpeeds", ImVec2(char_x * 30, char_x * 15), plot_flags)) {
    return;
  }

  ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_NoDecorations);
  ImPlot::SetupAxis(ImAxis_Y1, "Speed", ImPlotAxisFlags_NoDecorations);

  ImPlot::SetupAxisLimits(ImAxis_X1, 0, t_step * mouse_speeds.size(), ImPlotCond_Always);
  ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_speed, ImPlotCond_Always);

  // ImPlot::PlotShaded("Score History", times.data(), scores.data(), scores.size(), 0);
  ImPlot::PlotLineG("Speeds", point_getter, &mouse_data, mouse_speeds.size());

  /*
  if (ImPlot::IsPlotHovered()) {
    ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);

    float time = mouse_pos.x;

    int closest_index = std::round(time * kRecordScoresPerSecond);
    if (IsValidIndex(scores, closest_index)) {
      float x_val = mouse_pos.x;
      float y_val = scores[closest_index];
      ImGui::BeginTooltip();
      ImGui::Text("Score: %.2f", y_val);
      ImGui::Text("Time: %.2f", x_val);
      ImGui::EndTooltip();

      ImPlot::SetNextMarkerStyle(
          ImPlotMarker_Circle, 4.0f, ImVec4(1, 0, 0, 1), IMPLOT_AUTO, ImVec4(1, 0, 0, 1));
      ImPlot::PlotScatter("MouseDot", &x_val, &y_val, 1);
    }
  }
  */

  int closest_index = std::round(now / t_step);
  if (IsValidIndex(mouse_speeds, closest_index)) {
    ImPlotSpec spec;
    spec.Marker = ImPlotMarker_Circle;
    spec.MarkerSize = 4.0f;
    spec.MarkerFillColor = ImVec4(1, 0, 0, 1);
    spec.MarkerLineColor = ImVec4(1, 0, 0, 1);
    spec.LineWeight = IMPLOT_AUTO;
    float x_val = closest_index * t_step;
    float y_val = mouse_speeds[closest_index];
    ImPlot::PlotScatter("MouseDot", &x_val, &y_val, 1, spec);
  }

  ImPlot::EndPlot();
}

class ReplayView {
 public:
  ReplayView(std::shared_ptr<Replay> replay, Application& app)
      : camera(Camera(CameraParams(replay->room))),
        target_manager(replay->room),
        replay_(replay),
        micros_per_frame_(GetMicrosPerFrame(replay->replay_fps)),
        app_(app) {
    previous_click_durations_.reserve(400);
  }

  Camera camera;
  TargetManager target_manager;

  bool IsDone() {
    return is_done_;
  }

  i64 CurrentTimeMicros() {
    return current_time_micros_;
  }

  i64 GetCurrentFrameNumber() {
    return current_time_micros_ / micros_per_frame_;
  }

  i64 GetLastClickTimeMicros() {
    return last_click_time_micros_;
  }

  float GetCurrentScore() {
    i64 score_frame = MicrosToSeconds(current_time_micros_) * kRecordScoresPerSecond;
    if (IsValidIndex(replay_->scores, score_frame)) {
      return replay_->scores[score_frame];
    }
    return 0;
  }

  std::vector<float> GetPreviousClickDurations() {
    return previous_click_durations_;
  }

  void SeekForwardToTimeSeconds(float now_seconds, std::optional<SoundSettings> sound_settings) {
    SeekForwardToTimeMicros(now_seconds * 1000000, sound_settings);
  }

  void SeekForwardToTimeMicros(float now_micros, std::optional<SoundSettings> sound_settings) {
    if (is_done_) {
      return;
    }
    const Replay& replay = *replay_;
    const std::vector<ReplayEvent>& events = replay.events;

    if (replay_->replay_fps <= 0) {
      assert(false && "Replay FPS not set");
      is_done_ = true;
      return;
    }

    current_time_micros_ = now_micros;
    i64 replay_frame_number = now_micros / micros_per_frame_;

    for (int i = processed_targets_up_to_index_; i < replay.target_metadata.size(); ++i) {
      const ReplayTargetMetadata& metadata = replay.target_metadata[i];
      if (metadata.add_time_micros > now_micros) {
        break;
      }

      Target t;
      t.id = metadata.target_id;
      t.radius = metadata.initial_data.radius;
      t.position = metadata.initial_data.position;
      t.is_ghost = metadata.is_ghost;
      if (metadata.has_health) {
        // Health percent in the replay is represented as 0..255. We will use each increment as a
        // click to reuse the existing health mechanism for multi click.
        t.health_clicks = 255;
      }
      if (metadata.pill_height > 0) {
        t.is_pill = true;
        t.height = metadata.pill_height;
      }
      target_data_channel_map_[t.id] = metadata.data_channel;
      target_added_on_frame_[t.id] = replay_frame_number;
      if (t.is_ghost) {
        target_manager.MarkAllAsNonGhost();
      }
      target_manager.AddTarget(t);
      processed_targets_up_to_index_ = i + 1;
    }

    for (int i = processed_events_up_to_index_; i < events.size(); ++i) {
      const ReplayEvent& event = events[i];
      if (event.time_micros > now_micros) {
        break;
      }

      // Process the event.
      switch (event.type) {
        case ReplayEventType::REMOVE_TARGET:
          target_manager.RemoveTarget(event.data.target_id);
          target_data_channel_map_.erase(event.data.target_id);
          break;
        case ReplayEventType::PLAY_SOUND:
          if (sound_settings.has_value()) {
            app_.sound_manager().PlayLoadedSound(*sound_settings, event.data.play_sound.sound);
          }
          break;
        case ReplayEventType::MOUSE_CLICK:
          previous_click_durations_.push_back(
              MicrosToSeconds(event.time_micros - last_click_time_micros_));
          last_click_time_micros_ = event.time_micros;
          break;
      }
      processed_events_up_to_index_ = i + 1;
    }

    for (int i = processed_pitch_yaws_up_to_index_;
         i <= std::min<int>(replay_frame_number, replay.pitch_yaws.size() - 1);
         ++i) {
      const PitchYaw& pitch_yaw = replay.pitch_yaws[i];
      if (pitch_yaw.pitch < (GetMaxPitch() + 0.1f)) {
        camera.UpdatePitchYaw(pitch_yaw);
      }
      processed_pitch_yaws_up_to_index_ = i;
    }

    if (replay_frame_number >= replay.pitch_yaws.size()) {
      is_done_ = true;
      return;
    }

    {
      i64 start_index = replay_frame_number * replay.num_targets;
      int end_index = start_index + replay.num_targets;
      for (auto& entry : target_data_channel_map_) {
        u16 data_channel = entry.second;
        u16 target_id = entry.first;

        i64 added_on_frame = target_added_on_frame_[target_id];

        // Don't start updating until next frame after adding.
        bool old_enough_target = replay_frame_number > added_on_frame;
        Target* target = target_manager.GetMutableTarget(target_id);

        if (old_enough_target && target != nullptr) {
          i64 i = start_index + data_channel;
          if (IsValidIndex(replay.target_data, i)) {
            const ReplayTargetData& data = replay.target_data[i];
            if (data.radius > 0) {
              target->position = data.position;
              target->radius = data.radius;
              target->click_count = 255 - data.health;
            }
          }
        }
      }
    }
  }

 private:
  std::shared_ptr<Replay> replay_;
  int processed_events_up_to_index_ = 0;
  int processed_targets_up_to_index_ = 0;
  int processed_pitch_yaws_up_to_index_ = 0;
  std::unordered_map<u16, u16> target_data_channel_map_;
  std::unordered_map<u16, i64> target_added_on_frame_;
  Application& app_;
  bool is_done_ = false;
  i64 current_time_micros_ = 0;
  i64 last_click_time_micros_ = 0;
  i64 next_click_time_micros_ = 0;
  std::vector<float> previous_click_durations_;
  i64 micros_per_frame_ = 0;
};

class ReplayViewerScreen : public Screen {
 public:
  ReplayViewerScreen(std::shared_ptr<Replay> replay, Application* app)
      : Screen(*app),
        replay_(replay),
        timer_(replay->replay_fps),
        replay_view_(std::make_unique<ReplayView>(replay, *app)) {
    float approximate_mb = replay->GetApproximateSizeMb();
    settings_ = app->settings_manager().GetCurrentSettingsForScenario(replay->scenario_name);
    crosshair_ = app->settings_manager().GetCurrentCrosshair();
    theme_ = app->settings_manager().GetCurrentTheme();

    projection_ = GetPerspectiveTransformation(app_.screen_info(), replay->room.horizontal_fov());
    std::optional<int> existing_playback_speed = app->local_store().GetInt(kPlaybackSpeedKey);
    if (existing_playback_speed) {
      playback_speed_ = static_cast<PlaybackSpeed>(*existing_playback_speed);
    }

    for (const auto& event : replay->events) {
      if (event.type == ReplayEventType::MOUSE_CLICK) {
        has_click_events_ = true;
        break;
      }
    }

    mouse_speeds_ = GetMouseSpeeds(*replay);
    if (mouse_speeds_.size() > 0) {
      max_mouse_speed_ = *std::max_element(mouse_speeds_.begin(), mouse_speeds_.end());
    }
  }

  void OnEvents(std::span<SDL_Event> events) override {
    ImGuiIO& io = ImGui::GetIO();
    for (const SDL_Event& event : events) {
      if (IsQuitEvent(event)) {
        app_.RequestExit();
      }
      ImGui_ImplSDL3_ProcessEvent(&event);
      OnEvent(event, io.WantTextInput);
    }
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) {
    if (IsEscapeKeyDown(event)) {
      PopSelf();
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
      if (event.key.key == SDLK_SPACE) {
        if (replay_view_->IsDone()) {
          SeekToTimeMicros(0);
          Play();
        } else if (is_paused_) {
          Play();
        } else {
          Pause();
        }
      }
      i64 time_step_micros = 10000;  // 10mj
      if (event.key.key == SDLK_LEFT || event.key.key == SDLK_COMMA) {
        SeekToTimeMicros(GetNowMicros() - time_step_micros);
      }
      if (event.key.key == SDLK_RIGHT || event.key.key == SDLK_PERIOD) {
        SeekToTimeMicros(GetNowMicros() + time_step_micros);
      }
    }
  }

  void OnTick() override {
    const Replay& replay = *replay_;
    const std::vector<ReplayEvent>& events = replay.events;

    if (!is_paused_) {
      playback_stopwatch_.Start();
    }

    timer_.OnStartFrame();

    float duration_seconds = replay.GetDurationSeconds();

    i64 now_micros = GetNowMicros();
    replay_view_->SeekForwardToTimeMicros(now_micros, settings_.sounds());

    bool do_render = timer_.LastFrameRenderStartedMicrosAgo() > 2000;
    if (!do_render) {
      return;
    }

    timer_.OnStartRender();
    auto end_render_guard = absl::MakeCleanup([&] { timer_.OnEndRender(); });

    ImGui::NewSdlFrame();
    app_.BeginFullscreenWindow();
    app_.crosshair_renderer().Draw(
        crosshair_, settings_.crosshair_size(), theme_, app_.screen_info().center);

    float elapsed_seconds = timer_.GetElapsedSeconds();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("fps: %d", (int)ImGui::GetIO().Framerate);
    ImGui::SameLine();
    ImGui::InfoMarker(
        std::format("Approximate file size: {:.2f}mb", replay_->GetApproximateSizeMb()));

    ImGui::SameLine();
    ImGui::SetButtonCursorAtRight(icons::kLogout);
    if (ImGui::Button(icons::kLogout)) {
      PopSelf();
    }

    ImGui::Text("recorded fps: %d", replay_->replay_fps);

    float score = replay_view_->GetCurrentScore();
    if (score > 0) {
      ImGui::TextFmt("score: {}", MaybeIntToString(score, 2));
    }

    if (has_click_events_) {
      const auto& durations = replay_view_->GetPreviousClickDurations();
      ImGui::Text("click times");
      ImGui::Indent();
      if (durations.size() > 0) {
        for (int i = std::max<int>(0, durations.size() - 20); i < durations.size(); ++i) {
          ImGui::Text("%0.2fs", durations[i]);
        }
      }
      float click_now_seconds =
          MicrosToSeconds(now_micros - replay_view_->GetLastClickTimeMicros());
      ImGui::Text("%0.2fs", click_now_seconds);
      ImGui::Unindent();

      {
        // Draw the mouse speeds plot
        i64 micros_per_frame = GetMicrosPerFrame(replay_->replay_fps);
        float t_step = MicrosToSeconds(micros_per_frame);
        // Around 1 second of graph.
        int num_steps = 1.0 / t_step;

        i64 frame_start = replay_view_->GetLastClickTimeMicros() / micros_per_frame;
        i64 num_frames_remaining = mouse_speeds_.size() - frame_start;
        std::span<float> speeds =
            std::span(mouse_speeds_)
                .subspan(frame_start, std::min<int>(num_steps, num_frames_remaining));
        DrawMouseSpeedsPlot(click_now_seconds, t_step, speeds, max_mouse_speed_);
      }
    }

    i64 frame_number = replay_view_->GetCurrentFrameNumber();
    if (IsValidIndex(mouse_speeds_, frame_number)) {
      ImGui::TextFmt("mouse speed: {}", MaybeIntToString(mouse_speeds_[frame_number], 0));
    }

    ImGui::SetCursorAtBottom(ImGui::GetFrameHeight() * 1.5);

    float controls_width = app_.screen_info().width * 0.75;
    float controls_offset = (app_.screen_info().width - controls_width) * 0.5;
    ImGui::SetCursorPosX(controls_offset);
    ImGui::BeginChild("PlaybackControls", ImVec2(controls_width, 0));

    if (replay_view_->IsDone()) {
      if (ImGui::Button(icons::kReplay)) {
        SeekToTimeMicros(0);
        Play();
      }
    } else {
      bool is_playing = playback_stopwatch_.IsRunning();
      if (is_playing) {
        if (ImGui::Button(icons::kPause)) {
          Pause();
        }
      } else {
        if (ImGui::Button(icons::kPlayArrow)) {
          Play();
        }
      }
    }

    ImGui::SameLine();
    float char_x = ImGui::GetDefaultCharSizeX();
    if (ImGui::SimpleTypeDropdown("PlaybackSpeed", &playback_speed_, kPlaybackSpeeds, char_x * 6)) {
      playback_start_time_micros_ = now_micros;
      playback_stopwatch_ = Stopwatch();
      app_.local_store().PutInt(kPlaybackSpeedKey, static_cast<int>(playback_speed_));
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    float seek_time = MicrosToSeconds(now_micros);
    if (ImGui::SliderFloat("##SeekBar", &seek_time, 0.0f, duration_seconds, "%.1f")) {
      SeekToTimeMicros(SecondsToMicros(seek_time));
    }

    if (ImGui::IsItemClicked()) {
      float mouse_x = ImGui::GetMousePos().x;
      float bar_x_min = ImGui::GetItemRectMin().x;
      float bar_width = ImGui::GetItemRectSize().x;

      // Calculate the percentage of where the user clicked
      float t = (mouse_x - bar_x_min) / bar_width;
      SeekToTimeMicros(SecondsToMicros(duration_seconds * t));
    }

    ImGui::EndChild();
    ImGui::End();

    LookAtInfo look_at = replay_view_->camera.GetLookAt();
    app_.renderer().RenderScenario(projection_,
                                   replay.room,
                                   replay.shot_type,
                                   theme_,
                                   settings_.health_bar(),
                                   replay_view_->target_manager.GetTargets(),
                                   look_at);

    if (replay_view_->IsDone()) {
      Pause();
    }
  }

  void OnAttach() override {
    app_.SetPresentMode(settings_.present_mode());
    timer_.StartLoop();
    timer_.ResumeRun();
  }

 private:
  void Pause() {
    is_paused_ = true;
    playback_stopwatch_.Stop();
  }

  void Play() {
    is_paused_ = false;
    playback_stopwatch_.Start();
  }

  void SeekToTimeMicros(i64 now_micros, bool play_sounds = false) {
    now_micros = std::max<i64>(0, now_micros);

    float current_view_time_micros = replay_view_->CurrentTimeMicros();
    if (current_view_time_micros > now_micros) {
      // rewinding. need to create a new view.
      replay_view_ = std::make_unique<ReplayView>(replay_, app_);
    }
    replay_view_->SeekForwardToTimeMicros(
        now_micros, play_sounds ? settings_.sounds() : std::optional<SoundSettings>{});
    playback_start_time_micros_ = now_micros;
    playback_stopwatch_ = Stopwatch();
  }

  i64 GetNowMicros() {
    return playback_start_time_micros_ +
           (playback_stopwatch_.GetElapsedMicros() * GetPlaybackSpeedMultiplier());
  }

  float GetPlaybackSpeedMultiplier() {
    int speed_int = static_cast<int>(playback_speed_);
    return speed_int / 100.0f;
  }

  std::shared_ptr<Replay> replay_;

  ScenarioTimer timer_;
  glm::mat4 projection_;

  Theme theme_;
  Settings settings_;
  Crosshair crosshair_;

  std::unique_ptr<ReplayView> replay_view_;

  // The time associated with the playback stopwatch. time + stopwatch.elapsed = now
  float playback_start_time_micros_ = 0;
  Stopwatch playback_stopwatch_;
  bool is_paused_ = false;

  PlaybackSpeed playback_speed_ = PlaybackSpeed::SPEED_100;
  bool has_click_events_ = false;
  std::vector<float> mouse_speeds_;
  float max_mouse_speed_ = 0;
};

}  // namespace

std::unique_ptr<Screen> CreateReplayViewerScreen(std::shared_ptr<Replay> replay, Application* app) {
  return std::make_unique<ReplayViewerScreen>(std::move(replay), app);
}

}  // namespace aim
