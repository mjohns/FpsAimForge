#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "aim/audio/sound_manager.h"
#include "aim/common/simple_types.h"
#include "aim/proto/scenario.pb.h"

namespace aim {

inline constexpr const int kRecordScoresPerSecond = 10;

struct Target;

struct ReplayTargetData {
  float radius = -1;
  glm::vec3 position{};
  u8 health = 255;
};

enum class ReplayEventType : u16 {
  REMOVE_TARGET = 1,
  PLAY_SOUND = 2,
  MOUSE_CLICK = 3,
};

struct PlaySoundEvent {
  SoundType sound;
};

struct ReplayTargetMetadata {
  u32 add_time_micros;
  u16 target_id;
  u16 data_channel;
  ReplayTargetData initial_data;
  float pill_height;
  bool is_ghost = false;
  bool has_health = false;
};

union ReplayEventData {
  PlaySoundEvent play_sound;
  u16 target_id;
  bool is_hit;
};

struct ReplayEvent {
  ReplayEventType type;
  ReplayEventData data{};
  u32 time_micros = 0;
};

struct Replay {
  Room room;
  ShotType::TypeCase shot_type = ShotType::TYPE_NOT_SET;
  u16 replay_fps;
  u16 num_targets;
  std::string scenario_name;
  std::vector<ReplayEvent> events;
  std::vector<ReplayTargetData> target_data;
  std::vector<PitchYaw> pitch_yaws;
  std::vector<ReplayTargetMetadata> target_metadata;
  std::vector<float> scores;
  std::vector<glm::vec3> camera_positions;

  float GetApproximateSizeMb() const;
  float GetDurationSeconds() const;
};

class ReplayRecorder {
 public:
  ReplayRecorder(const std::string& scenario_name,
                 const Room& room,
                 ShotType::TypeCase shot_type,
                 u16 replay_fps,
                 i32 duration_seconds,
                 i32 num_targets,
                 bool requires_per_frame_target_data,
                 bool record_camera_position);

  void AddTarget(i64 now_micros, const Target& target);
  void RemoveTarget(i64 now_micros, u16 target_id);
  void PlaySound(i64 now_micros, SoundType sound);
  void SetPitchYaw(i64 frame_number, float pitch, float yaw);
  void SetCameraPosition(i64 frame_number, const glm::vec3& position);
  void AddMouseClick(i64 now_micros, bool is_hit);
  void AddScore(float score);

  void SnapshotTargets(i64 frame_number, const std::vector<Target>& targets);

  std::shared_ptr<Replay> replay() {
    return replay_;
  }

  // After the run is done call this to fill in  pitch_yaw from any missing frames.
  void FillInMissingPitchYaws();

 private:
  ReplayEvent& AddEvent(i64 now_micros, ReplayEventType type);
  i32 num_targets_;

  std::unordered_map<u16, u16> target_data_channel_map_;
  std::shared_ptr<Replay> replay_;
};

}  // namespace aim
