#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "SDL3/SDL.h"
#include "google/protobuf/text_format.h"
#include "aim/core/version.h"
#include "aim/proto/settings.pb.h"
#include "aim/proto/scenario.pb.h"
#include "aim/scenario/replay.h"

namespace aim {

// Bounded, in-memory capture. No filesystem I/O occurs on the input/event path.
class RawTelemetryRecorder {
 public:
  struct Event {
    std::uint64_t sdl_timestamp_ns = 0;
    std::int64_t run_time_micros = 0;
    std::uint32_t type = 0;
    float x = 0;
    float y = 0;
    float data1 = 0;
    float data2 = 0;
  };

  // Allocate the bounded buffer before gameplay begins so push_back does not grow it mid-run.
  RawTelemetryRecorder() = default;

  void Prepare() {
    if (events_.capacity() < kMaxEvents) {
      events_.reserve(kMaxEvents);
    }
  }

  bool HasRunStarted() const { return run_start_epoch_ms_ > 0; }

  void MarkRunStarted() {
    run_start_epoch_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
  }

  void Record(const SDL_Event& event, std::int64_t run_time_micros) {
    if (!IsRelevant(event.type)) return;
    if (events_.size() >= kMaxEvents) {
      ++dropped_events_;
      return;
    }

    Event record;
    record.sdl_timestamp_ns = event.common.timestamp;
    record.run_time_micros = run_time_micros;
    record.type = event.type;
    switch (event.type) {
      case SDL_EVENT_MOUSE_MOTION:
        record.x = event.motion.xrel;
        record.y = event.motion.yrel;
        record.data1 = event.motion.x;
        record.data2 = event.motion.y;
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        record.x = static_cast<float>(event.button.button);
        record.y = static_cast<float>(event.button.clicks);
        record.data1 = event.button.x;
        record.data2 = event.button.y;
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        record.x = event.wheel.x;
        record.y = event.wheel.y;
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
        record.x = static_cast<float>(event.key.key);
        record.y = event.key.repeat ? 1.0f : 0.0f;
        record.data1 = static_cast<float>(event.key.scancode);
        record.data2 = static_cast<float>(event.key.mod);
        break;
      default:
        return;
    }
    events_.push_back(record);
  }

  bool ExportText(const std::filesystem::path& path,
                  std::string_view scenario_name,
                  double cm_per_360,
                  double dpi,
                  double duration_seconds,
                  const ScenarioDef& scenario_def,
                  const Settings& settings,
                  const Replay* replay) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) return false;
    out.precision(std::numeric_limits<float>::max_digits10);
    std::string scenario_config;
    std::string settings_config;
    const bool has_scenario_config =
        google::protobuf::TextFormat::PrintToString(scenario_def, &scenario_config);
    const bool has_settings_config =
        google::protobuf::TextFormat::PrintToString(settings, &settings_config);
    out << "FPS AIM FORGE RAW AIM TELEMETRY\n"
        << "schema_version=1\n"
        << "app_version=" << kAimForgeVersion
        << "\npolling_rate_hz=not_exposed_by_application"
        << "\nrun_start_epoch_ms=" << run_start_epoch_ms_
        << "\nexported_at_epoch_ms="
        << std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count()
        << "\nscenario=" << scenario_name
        << "\ncm_per_360=" << cm_per_360
        << "\ndpi=" << dpi
        << "\nduration_seconds=" << duration_seconds
        << "\nraw_input_events=" << events_.size()
        << "\ndropped_input_events=" << dropped_events_
        << "\ninput_timestamp_units=SDL timestamp nanoseconds (source clock); run_time_micros=active scenario timer microseconds"
        << "\n\n[SCENARIO_CONFIG_PROTO_TEXT]\n"
        << (has_scenario_config ? scenario_config : "unavailable")
        << "\n\n[SETTINGS_PROTO_TEXT]\n"
        << (has_settings_config ? settings_config : "unavailable")
        << "\n\n[EVENT_FIELD_LEGEND]\n"
        << "mouse_motion: x=relative_dx, y=relative_dy, data1=absolute_x, data2=absolute_y\n"
        << "mouse_button_down/up: x=button_id, y=click_count, data1=cursor_x, data2=cursor_y\n"
        << "mouse_wheel: x=horizontal_delta, y=vertical_delta\n"
        << "key_down/up: x=keycode, y=repeat_flag, data1=scancode, data2=modifier_mask\n"
        << "\n[RAW_INPUT_EVENTS]\n"
        << "index\tsdl_timestamp_ns\trun_time_micros\tevent_type\tevent_name\tx\ty\tdata1\tdata2\n";
    for (std::size_t i = 0; i < events_.size(); ++i) {
      const Event& e = events_[i];
      out << i << '\t' << e.sdl_timestamp_ns << '\t' << e.run_time_micros << '\t'
          << e.type << '\t' << EventName(e.type) << '\t' << e.x << '\t' << e.y << '\t'
          << e.data1 << '\t' << e.data2 << '\n';
    }

    if (replay != nullptr) {
      out << "\n[REPLAY_METADATA]\n"
          << "replay_fps=" << replay->replay_fps
          << "\nshot_type=" << static_cast<int>(replay->shot_type)
          << "\nreplay_cm_per_360=" << replay->cm_per_360
          << "\nreplay_event_count=" << replay->events.size()
          << "\ntarget_metadata_count=" << replay->target_metadata.size()
          << "\ncamera_samples=" << replay->pitch_yaws.size()
          << "\ntarget_samples=" << replay->target_data.size()
          << "\n\n[CAMERA_PITCH_YAW_SAMPLES]\n"
          << "frame\tpitch\tyaw\n";
      for (std::size_t i = 0; i < replay->pitch_yaws.size(); ++i) {
        out << i << '\t' << replay->pitch_yaws[i].pitch << '\t'
            << replay->pitch_yaws[i].yaw << '\n';
      }
      out << "\n[TARGET_LIFECYCLE]\n"
          << "add_time_micros\ttarget_id\tdata_channel\tinitial_x\tinitial_y\tinitial_z\tradius\thealth\tis_ghost\thas_health\n";
      for (const ReplayTargetMetadata& t : replay->target_metadata) {
        out << t.add_time_micros << '\t' << t.target_id << '\t' << t.data_channel << '\t'
            << t.initial_data.position.x << '\t' << t.initial_data.position.y << '\t'
            << t.initial_data.position.z << '\t' << t.initial_data.radius << '\t'
            << static_cast<int>(t.initial_data.health) << '\t' << t.is_ghost << '\t'
            << t.has_health << '\n';
      }
      out << "\n[TARGET_FRAME_SAMPLES]\n"
          << "sample_index\tframe\tchannel\tvalid\tx\ty\tz\tradius\thealth_percent\n";
      for (std::size_t i = 0; i < replay->target_data.size(); ++i) {
        const ReplayTargetData& t = replay->target_data[i];
        const bool valid = t.radius >= 0.0f;
        const std::size_t frame = replay->num_targets > 0 ? i / replay->num_targets : 0;
        const std::size_t channel = replay->num_targets > 0 ? i % replay->num_targets : 0;
        out << i << '\t' << frame << '\t' << channel << '\t' << valid << '\t'
            << t.position.x << '\t' << t.position.y << '\t' << t.position.z << '\t'
            << t.radius << '\t' << (static_cast<float>(t.health) / 255.0f * 100.0f) << '\n';
      }
      out << "\n[SCORE_SAMPLES]\n"
          << "sample_index\tscore\n";
      for (std::size_t i = 0; i < replay->scores.size(); ++i) {
        out << i << '\t' << replay->scores[i] << '\n';
      }
      out << "\n[REPLAY_EVENTS]\n"
          << "time_micros\tevent_type\tvalue\n";
      for (const ReplayEvent& e : replay->events) {
        out << e.time_micros << '\t' << static_cast<int>(e.type) << '\t';
        switch (e.type) {
          case ReplayEventType::REMOVE_TARGET:
            out << e.data.target_id;
            break;
          case ReplayEventType::MOUSE_CLICK:
            out << (e.data.is_hit ? "hit" : "miss");
            break;
          case ReplayEventType::PLAY_SOUND:
            out << static_cast<int>(e.data.play_sound.sound);
            break;
        }
        out << '\n';
      }
    }
    out << "\n[DATA_QUALITY]\n"
        << "events_dropped_due_to_capacity=" << dropped_events_
        << "\nraw_input_capture_enabled=true\n"
        << "replay_data_available=" << (replay != nullptr) << "\n"
        << "scenario_config_available=" << has_scenario_config << "\n"
        << "settings_config_available=" << has_settings_config << "\n"
        << "note=No raw events were sent to any remote service by this exporter.\n";
    out.flush();
    return out.good();
  }

 private:
  static constexpr std::size_t kMaxEvents = 500000;

  static std::string_view EventName(std::uint32_t type) {
    switch (type) {
      case SDL_EVENT_MOUSE_MOTION: return "mouse_motion";
      case SDL_EVENT_MOUSE_BUTTON_DOWN: return "mouse_button_down";
      case SDL_EVENT_MOUSE_BUTTON_UP: return "mouse_button_up";
      case SDL_EVENT_MOUSE_WHEEL: return "mouse_wheel";
      case SDL_EVENT_KEY_DOWN: return "key_down";
      case SDL_EVENT_KEY_UP: return "key_up";
      default: return "other";
    }
  }

  static bool IsRelevant(std::uint32_t type) {
    return type == SDL_EVENT_MOUSE_MOTION ||
           type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           type == SDL_EVENT_MOUSE_BUTTON_UP ||
           type == SDL_EVENT_MOUSE_WHEEL ||
           type == SDL_EVENT_KEY_DOWN ||
           type == SDL_EVENT_KEY_UP;
  }

  std::vector<Event> events_;
  std::uint64_t dropped_events_ = 0;
  std::int64_t run_start_epoch_ms_ = 0;
};

}  // namespace aim
