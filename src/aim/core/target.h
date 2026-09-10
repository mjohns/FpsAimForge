#pragma once

#include <optional>
#include <vector>

#include "aim/common/random.h"
#include "aim/common/simple_types.h"
#include "aim/common/times.h"
#include "aim/core/camera.h"
#include "aim/core/profile_selection.h"
#include "aim/proto/scenario.pb.h"
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {

struct RadiusAtKill {
  float start_radius;
  float end_radius;
};

struct TargetGrowthInfo {
  float grow_time_seconds;
  float time_at_final_size_seconds = 0;
  float start_time_seconds;
  float start_radius;
  float end_radius;
};

struct Target;

class MovementController {
 public:
  MovementController() {}
  virtual ~MovementController() {}

  virtual void UpdatePosition(float now_seconds,
                              Target& t,
                              const Room& room,
                              float delta_seconds) = 0;
};

struct Target {
  u16 id = 0;
  glm::vec3 position{};
  std::optional<glm::vec2> wall_position;
  float wall_depth = 0;
  glm::vec3 GetWallPosition3();

  float radius = 1.0f;

  std::shared_ptr<MovementController> movement_controller;
  float last_update_time_seconds = -1;
  float speed = 0;
  float acceleration = 0;

  float remove_after_time_seconds = -1;
  bool kill_sound_played = false;

  bool is_hit = false;

  bool hidden = false;
  bool is_ghost = false;

  bool is_pill = false;
  glm::vec3 pill_up{0, 0, 1};
  float height = 3.0f;

  float GetHealthPercent() const;
  void StopHitTimer();
  void StartHitTimer();
  void StopAllTimers();
  void AddTestDamage();

  float health_seconds = 0;
  float health_regen_rate = 0;
  std::optional<RadiusAtKill> radius_at_kill{};

  int health_clicks = -1;
  int click_count = 0;

  bool HasHealth() const {
    return health_seconds > 0 || health_clicks > 0;
  }

  std::optional<TargetGrowthInfo> growth_info{};

  void SetWallPosition(const glm::vec3& p, const Room& room);

  bool CanHit() const {
    return !hidden && !is_ghost;
  }

  bool ShouldDraw() const {
    return !hidden;
  }

 private:
  void MaybeResetTimersForRegen();

  Stopwatch hit_timer_;
  Stopwatch regen_timer_;
};

glm::vec3 WallPositionToWorldPosition(const glm::vec2& wall_position,
                                      float target_radius,
                                      const Room& room,
                                      float depth = 0);

class TargetManager {
 public:
  explicit TargetManager(const Room& room) : room_(room) {}

  Target AddTarget(Target t);
  bool RemoveTarget(u16 target_id);

  void UpdateRoom(const Room& room) {
    room_ = room;
  }

  void UpdateTargetPositions(float now_seconds);

  Target* GetMutableTarget(u16 target_id);
  Target* GetMutableMostRecentlyAddedTarget();
  std::vector<Target*> GetMutableVisibleTargets();

  TargetProfile GetTargetProfile(const TargetDef& def, Random& rand);

  void MarkAllAsNonGhost();

  std::vector<u16> visible_target_ids() const;

  void Clear() {
    targets_.clear();
  }

  const std::vector<Target>& GetTargets() {
    return targets_;
  }

  std::vector<Target>& GetMutableTargets() {
    return targets_;
  }

  u16 GetTargetIdCounter() {
    return target_id_counter_;
  }

  const std::optional<Target>& GetLastRemovedTarget() {
    return last_removed_target_;
  }

  std::optional<u16> GetNearestHitTarget(const Camera& camera, const glm::vec3& look_at);
  std::optional<u16> GetNearestTargetOnMiss(const Camera& camera, const glm::vec3& look_at);

 private:
  u16 target_id_counter_ = 0;
  std::vector<Target> targets_;
  std::optional<Target> last_removed_target_;
  Room room_;
  ProfileSelectionContext selection_context_;
};

}  // namespace aim
