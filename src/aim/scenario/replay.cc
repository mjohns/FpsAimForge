#include "replay.h"

#include <cassert>

#include "aim/core/target.h"

namespace aim {

ReplayRecorder::ReplayRecorder(const std::string& scenario_name,
                               const Room& room,
                               ShotType::TypeCase shot_type,
                               u16 replay_fps,
                               i32 duration_seconds,
                               i32 num_targets,
                               bool requires_per_frame_target_data,
                               bool record_camera_position)
    : num_targets_(num_targets) {
  replay_ = std::make_shared<Replay>();
  replay_->scenario_name = scenario_name;
  replay_->room = room;
  replay_->replay_fps = replay_fps;
  replay_->num_targets = num_targets;
  replay_->shot_type = shot_type;

  i32 max_replay_frame_number = replay_fps * duration_seconds;
  i32 total_target_data_count = max_replay_frame_number * num_targets;

  if (requires_per_frame_target_data) {
    ReplayTargetData invalid_target_data;
    invalid_target_data.radius = -1;
    replay_->target_data.resize(total_target_data_count, invalid_target_data);
  }

  PitchYaw invalid_pitch_yaw;
  invalid_pitch_yaw.pitch = GetMaxPitch() * 3;
  replay_->pitch_yaws.resize(max_replay_frame_number, invalid_pitch_yaw);
  if (record_camera_position) {
    replay_->camera_positions.reserve(max_replay_frame_number);
  }

  // Make this large enough that it reasonably won't need to allocate more memory during a run.
  // Maybe we should drop certain event types (like sound) if it is approaching the limit.
  replay_->events.reserve(3000);
  replay_->target_metadata.reserve(500);

  replay_->scores.reserve(130 * kRecordScoresPerSecond);
}

void ReplayRecorder::FillInMissingPitchYaws() {
  float max_pitch = GetMaxPitch();

  auto find_first_valid = [=, this](int i) {
    for (; i < replay_->pitch_yaws.size(); ++i) {
      PitchYaw& pitch_yaw = replay_->pitch_yaws[i];
      bool is_invalid = pitch_yaw.pitch > max_pitch;
      if (!is_invalid) {
        return pitch_yaw;
      }
    }
    return PitchYaw{};
  };

  for (int i = 0; i < replay_->pitch_yaws.size(); ++i) {
    PitchYaw& pitch_yaw = replay_->pitch_yaws[i];
    bool is_invalid = pitch_yaw.pitch > max_pitch;
    if (is_invalid) {
      if (i == 0) {
        pitch_yaw = find_first_valid(i + 1);
      } else {
        const PitchYaw& prev = replay_->pitch_yaws[i - 1];
        pitch_yaw.pitch = prev.pitch;
        pitch_yaw.yaw = prev.yaw;
      }
    }
  }
}

void ReplayRecorder::AddTarget(i64 now_micros, const Target& target) {
  // Find available data channel.
  std::vector<bool> taken_channels(num_targets_, false);
  for (auto& entry : target_data_channel_map_) {
    taken_channels[entry.second] = true;
  }

  int available_channel = 0;
  for (; available_channel < num_targets_; ++available_channel) {
    if (!taken_channels[available_channel]) {
      break;
    }
  }

  target_data_channel_map_[target.id] = available_channel;

  replay_->target_metadata.push_back({});
  ReplayTargetMetadata& metadata = replay_->target_metadata.back();
  metadata.add_time_micros = now_micros;
  metadata.target_id = target.id;
  metadata.data_channel = available_channel;
  metadata.initial_data.position = target.position;
  metadata.initial_data.radius = target.radius;
  metadata.is_ghost = target.is_ghost;
  if (target.is_pill) {
    metadata.pill_height = target.height;
  }
  if (target.health_seconds > 0) {
    metadata.has_health = true;
  }
}

void ReplayRecorder::PlaySound(i64 now_micros, SoundType sound) {
  ReplayEvent& event = AddEvent(now_micros, ReplayEventType::PLAY_SOUND);
  event.data.play_sound.sound = sound;
}

void ReplayRecorder::RemoveTarget(i64 now_micros, u16 target_id) {
  ReplayEvent& event = AddEvent(now_micros, ReplayEventType::REMOVE_TARGET);
  event.data.target_id = target_id;
  target_data_channel_map_.erase(target_id);
}

void ReplayRecorder::AddMouseClick(i64 now_micros, bool is_hit) {
  ReplayEvent& event = AddEvent(now_micros, ReplayEventType::MOUSE_CLICK);
  event.data.is_hit = is_hit;
}

void ReplayRecorder::AddScore(float score) {
  replay_->scores.push_back(score);
}

ReplayEvent& ReplayRecorder::AddEvent(i64 now_micros, ReplayEventType type) {
  replay_->events.push_back({});
  ReplayEvent& event = replay_->events.back();
  event.time_micros = now_micros;
  event.type = type;
  return event;
}

void ReplayRecorder::SetPitchYaw(i64 frame_number, float pitch, float yaw) {
  if (frame_number < replay_->pitch_yaws.size()) {
    PitchYaw& val = replay_->pitch_yaws[frame_number];
    val.pitch = pitch;
    val.yaw = yaw;
  }
}

void ReplayRecorder::SetCameraPosition(i64 frame_number, const glm::vec3& position) {
  size_t num_to_add = (frame_number - replay_->camera_positions.size()) + 1;
  if (num_to_add > 0) {
    replay_->camera_positions.insert(replay_->camera_positions.end(), num_to_add, position);
  }
}

void ReplayRecorder::SnapshotTargets(i64 frame_number, const std::vector<Target>& targets) {
  int start_index = frame_number * num_targets_;
  int end_index = start_index + num_targets_;

  bool is_valid_max_index = end_index <= replay_->target_data.size();
  if (!is_valid_max_index) {
    return;
  }

  assert(targets.size() <= num_targets_ && "Too many targets");
  for (const Target& target : targets) {
    if (target.hidden) {
      continue;
    }
    auto it = target_data_channel_map_.find(target.id);
    if (it != target_data_channel_map_.end()) {
      u16 data_channel = it->second;
      assert(data_channel < num_targets_ && "Invalid data channel");
      if (data_channel < num_targets_) {
        ReplayTargetData& data = replay_->target_data[start_index + data_channel];
        data.position = target.position;
        data.radius = target.radius;
        data.health = std::round(target.GetHealthPercent() * 255);
      }
    }
  }
}

float Replay::GetApproximateSizeMb() const {
  i64 size_bytes = pitch_yaws.size() * sizeof(PitchYaw);
  size_bytes += target_data.size() * sizeof(ReplayTargetData);
  size_bytes += target_metadata.size() * sizeof(ReplayTargetMetadata);
  size_bytes += events.size() * sizeof(ReplayEvent);
  size_bytes += scores.size() * sizeof(float);
  size_bytes += camera_positions.size() * sizeof(float) * 3;

  return size_bytes / 1000000.0f;
}

float Replay::GetDurationSeconds() const {
  return pitch_yaws.size() / static_cast<float>(replay_fps);
}

}  // namespace aim
