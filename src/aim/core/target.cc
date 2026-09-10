#include "target.h"

#include "aim/common/geometry.h"
#include "aim/common/util.h"
#include "aim/core/profile_selection.h"

namespace aim {

void Target::SetWallPosition(const glm::vec3& p, const Room& room) {
  wall_position = p;
  wall_depth = p.z;
  position = WallPositionToWorldPosition(*wall_position, radius, room, wall_depth);
}

Target TargetManager::AddTarget(Target t) {
  if (t.wall_position.has_value()) {
    t.position = WallPositionToWorldPosition(*t.wall_position, t.radius, room_, t.wall_depth);
  }
  if (t.id == 0) {
    auto new_id = ++target_id_counter_;
    t.id = new_id;
  }

  // Try to overwrite a hidden target.
  for (int i = 0; i < targets_.size(); ++i) {
    if (targets_[i].hidden) {
      targets_[i] = t;
      return t;
    }
  }
  targets_.push_back(t);
  return t;
}

Target* TargetManager::GetMutableTarget(u16 target_id) {
  for (Target& t : targets_) {
    if (t.id == target_id) {
      return &t;
    }
  }
  return nullptr;
}

Target* TargetManager::GetMutableMostRecentlyAddedTarget() {
  Target* result = nullptr;
  u16 newest_target_id = 0;

  for (Target& t : targets_) {
    if (t.id > newest_target_id) {
      result = &t;
      newest_target_id = t.id;
    }
  }
  return result;
}

std::vector<Target*> TargetManager::GetMutableVisibleTargets() {
  std::vector<Target*> result;
  for (Target& t : targets_) {
    if (t.ShouldDraw()) {
      result.push_back(&t);
    }
  }
  return result;
}

bool TargetManager::RemoveTarget(uint16_t target_id) {
  bool removed = false;
  for (Target& t : targets_) {
    if (t.id == target_id) {
      last_removed_target_ = t;
      t.hidden = true;
      t.id = 0;
      removed = true;
    }
  }
  return removed;
}

void TargetManager::MarkAllAsNonGhost() {
  for (Target& t : targets_) {
    t.is_ghost = false;
  }
}

void TargetManager::UpdateTargetPositions(float now_seconds) {
  for (Target& t : targets_) {
    if (t.movement_controller && t.ShouldDraw()) {
      if (t.last_update_time_seconds < 0) {
        t.last_update_time_seconds = now_seconds;
      } else {
        float delta_seconds = now_seconds - t.last_update_time_seconds;
        t.movement_controller->UpdatePosition(now_seconds, t, room_, delta_seconds);
        t.last_update_time_seconds = now_seconds;
      }
    }
  }
}

std::optional<uint16_t> TargetManager::GetNearestHitTarget(const Camera& camera,
                                                           const glm::vec3& look_at) {
  std::optional<uint16_t> closest_hit_target_id;
  float closest_hit_distance = 0.0f;

  for (auto& target : targets_) {
    if (!target.CanHit()) {
      continue;
    }
    bool is_hit = false;
    float hit_distance;
    float radius = target.radius;
    if (target.is_pill) {
      Pill pill;
      pill.position = target.position;
      pill.radius = radius;
      pill.height = target.height;
      pill.up = target.pill_up;
      is_hit = IntersectRayPill(pill, camera.GetPosition(), look_at, &hit_distance);
    } else {
      is_hit =
          IntersectRaySphere(target.position, radius, camera.GetPosition(), look_at, &hit_distance);
    }
    if (is_hit) {
      if (!closest_hit_target_id.has_value() || hit_distance < closest_hit_distance) {
        closest_hit_distance = hit_distance;
        closest_hit_target_id = target.id;
      }
    }
  }
  return closest_hit_target_id;
}

std::optional<uint16_t> TargetManager::GetNearestTargetOnMiss(const Camera& camera,
                                                              const glm::vec3& look_at) {
  if (targets_.size() == 0) {
    return {};
  }

  std::optional<float> closest_distance;
  std::optional<uint16_t> closest_target_id;
  for (auto& target : targets_) {
    if (target.CanHit()) {
      std::optional<float> distance =
          GetNormalizedMissedShotDistance(camera.GetPosition(), look_at, target.position);
      bool is_closest = false;
      if (distance.has_value()) {
        if (closest_distance.has_value()) {
          is_closest = *closest_distance > *distance;
        } else {
          is_closest = true;
        }
      }
      if (is_closest) {
        closest_target_id = target.id;
        closest_distance = distance;
      }
    }
  }
  return closest_target_id;
}

std::vector<u16> TargetManager::visible_target_ids() const {
  std::vector<u16> ids;
  for (auto& target : targets_) {
    if (target.ShouldDraw()) {
      ids.push_back(target.id);
    }
  }
  return ids;
}

TargetProfile TargetManager::GetTargetProfile(const TargetDef& def, Random& rand) {
  selection_context_.counter = target_id_counter_;
  auto maybe_profile =
      SelectProfile(def.profiles_info(), def.profiles(), &selection_context_, rand);
  if (maybe_profile.has_value()) {
    return *maybe_profile;
  }
  TargetProfile t;
  t.set_target_radius(4);
  return t;
}

glm::vec3 WallPositionToWorldPosition(const glm::vec2& wall_position,
                                      float target_radius,
                                      const Room& room,
                                      float depth) {
  // Make sure the target does not clip through wall and use depth if greater.
  // Min depth is 4.
  depth = std::max(depth, std::max(target_radius + 0.5f, 4.0f));

  glm::vec3 world_position;
  world_position.z = wall_position.y;
  if (room.has_simple_room() || room.has_barrel_room()) {
    world_position.x = wall_position.x;
    world_position.z = wall_position.y;

    world_position.y = -1 * depth;
  } else if (room.has_cylinder_room()) {
    // Effectively wrap the wall around the perimeter of the circle.
    float radius = room.cylinder_room().radius();
    // float circumference = glm::two_pi<float>() * radius;
    float radians_per_x = 1 / radius;

    glm::vec2 to_rotate(0, radius - depth);
    glm::vec2 rotated = RotateRadians(to_rotate, -1 * wall_position.x * radians_per_x);

    world_position.x = rotated.x;
    world_position.y = rotated.y;
  }

  return world_position;
}

float Target::GetHealthPercent() const {
  if (health_clicks >= 0) {
    float remaining_clicks = health_clicks - click_count;
    return remaining_clicks / (float)health_clicks;
  }

  if (health_seconds <= 0) {
    return 1;
  }
  float regen_penalty_seconds = 0;
  if (health_regen_rate > 0) {
    regen_penalty_seconds = regen_timer_.GetElapsedSeconds() * (health_regen_rate * health_seconds);
  }
  float elapsed_seconds = ClampPositive(hit_timer_.GetElapsedSeconds() - regen_penalty_seconds);
  float value = (health_seconds - elapsed_seconds) / health_seconds;
  return glm::clamp<float>(value, 0, 1);
}

void Target::StopHitTimer() {
  hit_timer_.Stop();
  regen_timer_.Start();
  MaybeResetTimersForRegen();
}

void Target::StopAllTimers() {
  hit_timer_.Stop();
  regen_timer_.Stop();
}

void Target::MaybeResetTimersForRegen() {
  if (health_regen_rate > 0 &&
      hit_timer_.GetElapsedSeconds() -
              (health_seconds * health_regen_rate * regen_timer_.GetElapsedSeconds()) <
          0) {
    // Reset the timers as health regenerated past 0.
    regen_timer_ = Stopwatch();
    hit_timer_ = Stopwatch();
  }
}

void Target::StartHitTimer() {
  regen_timer_.Stop();
  MaybeResetTimersForRegen();
  hit_timer_.Start();
}

void Target::AddTestDamage() {
  hit_timer_.AddElapsedSeconds(1);
}

glm::vec3 Target::GetWallPosition3() {
  if (!wall_position.has_value()) {
    return glm::vec3(0.0f);
  }
  return glm::vec3(wall_position->x, wall_position->y, wall_depth);
}

}  // namespace aim
