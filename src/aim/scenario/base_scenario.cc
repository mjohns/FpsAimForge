#include "base_scenario.h"

#include <memory>

#include "aim/common/geometry.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/times.h"
#include "aim/core/application.h"
#include "aim/scenario/scenario.h"
#include "glm/gtc/constants.hpp"  // IWYU pragma: keep
#include "glm/mat4x4.hpp"         // IWYU pragma: keep
#include "glm/trigonometric.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"           // IWYU pragma: keep

namespace aim {
namespace {
constexpr const float kPokeBallKillTimeSeconds = 0.1;

float GetPartialHitValue(const Target& target) {
  return 1 - target.GetHealthPercent();
}

}  // namespace

void BaseScenario::Initialize() {
  available_shots_ = def_.shot_type().reload().max_shots();
  const TargetDef& t = def_.target_def();
  int num_targets_to_add_immediately = t.num_targets() - t.delayed_target_times_size();
  for (int i = 0; i < num_targets_to_add_immediately; ++i) {
    float stagger_seconds = def_.target_def().stagger_initial_targets_seconds();
    if (stagger_seconds > 0) {
      RunAfterSeconds(i * stagger_seconds, [this]() { AddNewTarget(0, true); });
    } else {
      AddNewTarget(0, true);
    }
  }

  for (float delay : t.delayed_target_times()) {
    if (delay > 0) {
      RunAfterSeconds(delay, [this]() { AddNewTarget(0, true); });
    } else {
      AddNewTarget(0, true);
    }
  }
}

void BaseScenario::UpdateState(UpdateStateData* data) {
  auto shot_type = GetShotType();
  float now_seconds = timer_.GetElapsedSeconds();
  std::vector<u16> targets_to_remove;

  // Ghost bounds.
  float ghost_border_percent = def_.target_def().ghost_border_percent();
  if (ghost_border_percent > 0) {
    if (def_.room().has_barrel_room()) {
      float good_radius = def_.room().barrel_room().radius() * (1 - ghost_border_percent);
      for (Target& target : target_manager_.GetMutableTargets()) {
        if (target.wall_position) {
          float radial_distance = glm::length(*target.wall_position);
          bool in_bounds = radial_distance < good_radius;
          if (in_bounds) {
            target.is_ghost = false;
          } else {
            target.is_ghost = true;
          }
        }
      }
    } else {
      // Normal width/height room
      Wall wall = Wall::ForRoom(def_.room());
      float x_max = 0.5 * wall.width * (1 - ghost_border_percent);
      float x_min = -1 * x_max;

      float y_max = 0.5 * wall.height * (1 - ghost_border_percent);
      float y_min = -1 * y_max;

      for (Target& target : target_manager_.GetMutableTargets()) {
        if (target.wall_position) {
          const glm::vec2& p = *target.wall_position;
          bool in_bounds = p.x > x_min && p.x < x_max && p.y < y_max && p.y > y_min;
          if (in_bounds) {
            target.is_ghost = false;
          } else {
            target.is_ghost = true;
          }
        }
      }
    }
  }

  if (shot_type == ShotType::kTrackingKill || shot_type == ShotType::kTrackingInvincible) {
    HandleTrackingHits(data, &targets_to_remove);
  } else if (shot_type == ShotType::kTrackingProximity) {
    HandleProximityTrackingHits(data);
  } else if (shot_type == ShotType::kPoke) {
    HandlePokeHits(data);
  } else if (shot_type == ShotType::kPokeInstant) {
    HandlePokeInstantHits(data);
  } else {
    HandleClickHits(data);
  }
  if (def_.target_def().remove_target_after_seconds() > 0) {
    for (const Target& target : target_manager_.GetTargets()) {
      if (target.ShouldDraw() && target.remove_after_time_seconds < timer_.GetElapsedSeconds()) {
        targets_to_remove.push_back(target.id);
      }
    }
  }
  // Handle target growth
  for (Target& target : target_manager_.GetMutableTargets()) {
    if (target.growth_info.has_value()) {
      auto g = *target.growth_info;
      float delta_seconds = now_seconds - g.start_time_seconds;
      bool is_growing = delta_seconds < g.grow_time_seconds;
      if (is_growing) {
        // Change radius.
        float radius_range = std::abs(g.start_radius - g.end_radius);
        float rate = radius_range / g.grow_time_seconds;
        if (g.start_radius < g.end_radius) {
          target.radius = g.start_radius + (rate * delta_seconds);
        } else {
          target.radius = g.start_radius - (rate * delta_seconds);
        }
      } else {
        if (delta_seconds >= (g.grow_time_seconds + g.time_at_final_size_seconds)) {
          targets_to_remove.push_back(target.id);
        }
      }
    }
  }

  for (u16 target_id : targets_to_remove) {
    Target* target = target_manager_.GetMutableTarget(target_id);
    if (target != nullptr && ShouldCountPartialKills()) {
      stats_.num_hits += GetPartialHitValue(*target);
    }
    AddNewTarget(target_id);
  }

  UpdateTargetPositions();
  AddReplayScores(now_seconds);
}

void BaseScenario::AddReplayScores(float now_seconds) {
  if (replay_) {
    i64 score_frame = now_seconds * kRecordScoresPerSecond;
    if (last_recorded_score_frame_ < score_frame) {
      float score = CalculateScore(now_seconds);
      for (int i = last_recorded_score_frame_ + 1; i <= score_frame; ++i) {
        replay_->AddScore(CalculateScore(now_seconds));
      }
      last_recorded_score_frame_ = score_frame;
    }
  }
}

void BaseScenario::HandleProximityTrackingHits(UpdateStateData* data) {
  i64 now_micros = timer_.GetElapsedMicros();
  i64 delta_micros = now_micros - last_proximity_tracking_update_time_micros_;
  last_proximity_tracking_update_time_micros_ = now_micros;

  if (data->is_click_held) {
    if (!proximity_tracking_sound_) {
      proximity_tracking_sound_ =
          std::make_unique<ProximityTrackingSound>(settings_.sounds(),
                                                   settings_.proximity_min_shots_per_second(),
                                                   settings_.proximity_max_shots_per_second());
    }
    std::optional<float> normalized_distance_from_center;
    const auto& targets = target_manager_.GetTargets();
    if (targets.size() > 0) {
      const Target& target = targets[0];
      glm::vec3 position = target.position;

      std::optional<float> maybe_distance =
          target.is_pill
              ? GetPillMissedShotDistance(camera_.GetPosition(),
                                          camera_.GetLookAt().front,
                                          position,
                                          target.height - target.radius)
              : GetMissedShotDistance(camera_.GetPosition(), camera_.GetLookAt().front, position);

      if (maybe_distance) {
        float max_distance = target.radius;
        float value = (max_distance - *maybe_distance) / max_distance;
        if (value > 0) {
          // Max value for score is 750.
          stats_.num_hits += delta_micros * ((value + 0.05) * 0.00001);
          normalized_distance_from_center = 1.0f - value;

          stats_.proximity.hit_micros_map[100] += delta_micros;
          int comparison_distance = *normalized_distance_from_center * 100;
          for (int i = 1; i <= 9; ++i) {
            int percent_key = i * 10;
            if (comparison_distance <= percent_key) {
              stats_.proximity.hit_micros_map[percent_key] += delta_micros;
            }
          }
        }
      }
    }

    proximity_tracking_sound_->DoTick(
        timer_.GetElapsedMicros(), normalized_distance_from_center, replay_.get());
  } else {
    TrackingHoldDone();
  }
}

void BaseScenario::HandleTrackingHits(UpdateStateData* data,
                                      std::vector<u16>* target_ids_to_remove) {
  if (data->is_click_held) {
    auto maybe_hit_target_id = target_manager_.GetNearestHitTarget(camera_, look_at_.front);
    if (!tracking_sound_) {
      tracking_sound_ = std::make_unique<TrackingSound>(settings_.sounds(),
                                                        settings_.tracking_shots_per_second());
    }
    stats_.shot_stopwatch.Start();
    if (maybe_hit_target_id.has_value()) {
      stats_.hit_stopwatch.Start();
    } else {
      stats_.hit_stopwatch.Stop();
    }
    if (GetShotType() == ShotType::kTrackingKill) {
      for (Target& target : target_manager_.GetMutableTargets()) {
        if (maybe_hit_target_id.has_value() && *maybe_hit_target_id == target.id) {
          target.StartHitTimer();
          target.is_hit = true;
        } else {
          target.is_hit = false;
          target.StopHitTimer();
        }
        float remove_if_below_health_seconds = def_.shot_type().remove_if_below_health_seconds();
        if (remove_if_below_health_seconds > 0) {
          float remaining_health_seconds = target.GetHealthPercent() * target.health_seconds;
          if (remaining_health_seconds <= remove_if_below_health_seconds) {
            if (!target.kill_sound_played) {
              PlaySound(SoundType::TRACKING_KILL);
              target.kill_sound_played = true;
            }
          }
        }
        if (target.health_seconds > 0) {
          float health_percent = target.GetHealthPercent();
          if (health_percent <= 0) {
            stats_.num_hits++;
            stats_.num_kills++;
            if (!target.kill_sound_played) {
              PlaySound(SoundType::TRACKING_KILL);
              target.kill_sound_played = true;
            }
            AddNewTarget(target.id);
          }
        }
      }
    }
    tracking_sound_->DoTick(
        timer_.GetElapsedMicros(), maybe_hit_target_id.has_value(), replay_.get());
  } else {
    TrackingHoldDone();
  }

  if (GetShotType() == ShotType::kTrackingKill) {
    for (Target& target : target_manager_.GetMutableTargets()) {
      if (target.health_seconds > 0 && target.radius_at_kill.has_value()) {
        float radius_diff = target.radius_at_kill->start_radius - target.radius_at_kill->end_radius;
        float health_percent = target.GetHealthPercent();
        target.radius = target.radius_at_kill->end_radius + health_percent * radius_diff;
      }

      // See if for tracking kill we are no longer on target and it has low enough health to be
      // removed.
      float remove_if_below_health_seconds = def_.shot_type().remove_if_below_health_seconds();
      if (remove_if_below_health_seconds > 0 && !target.is_hit) {
        float remaining_health_seconds = target.GetHealthPercent() * target.health_seconds;
        if (remaining_health_seconds <= remove_if_below_health_seconds) {
          target_ids_to_remove->push_back(target.id);
        }
      }
    }
  }
}

void BaseScenario::OnScenarioDone() {
  stats_.hit_stopwatch.Stop();
  stats_.shot_stopwatch.Stop();
  TrackingHoldDone();
  if (ShouldCountPartialKills()) {
    float partial_kills = 0;
    for (Target& target : target_manager_.GetMutableTargets()) {
      partial_kills += GetPartialHitValue(target);
    }
    stats_.num_hits += partial_kills;
  }

  AddReplayScores(def_.duration_seconds());
}

bool BaseScenario::ShouldCountPartialKills() {
  return GetShotType() == ShotType::kTrackingKill && !def_.shot_type().no_partial_kills();
}

bool BaseScenario::ShouldLimitShotRate() {
  return def_.shot_type().click_rate_seconds() > 0;
}

void BaseScenario::TrackingHoldDone() {
  stats_.shot_stopwatch.Stop();
  stats_.hit_stopwatch.Stop();
  tracking_sound_ = {};
  proximity_tracking_sound_ = {};
  if (GetShotType() == ShotType::kTrackingKill) {
    for (Target& target : target_manager_.GetMutableTargets()) {
      target.is_hit = false;
      if (is_done()) {
        target.StopAllTimers();
      } else {
        target.StopHitTimer();
      }
    }
  }
}

void BaseScenario::OnPause() {
  TrackingHoldDone();
}

void BaseScenario::HandlePokeInstantHits(UpdateStateData* data) {
  auto maybe_hit_target_id = target_manager_.GetNearestHitTarget(camera_, look_at_.front);
  if (maybe_hit_target_id.has_value()) {
    stats_.num_hits++;
    stats_.num_shots++;
    stats_.num_kills++;
    PlaySound(SoundType::CLICK_KILL);
    data->force_render = true;

    auto hit_target_id = *maybe_hit_target_id;
    AddNewTarget(hit_target_id);

    if (replay_) {
      replay_->AddMouseClick(timer_.GetElapsedMicros(), true);
    }
  }
}

void BaseScenario::HandlePokeHits(UpdateStateData* data) {
  auto maybe_hit_target_id = target_manager_.GetNearestHitTarget(camera_, look_at_.front);
  if (!maybe_hit_target_id.has_value()) {
    // No current target. Clear.
    current_poke_target_id_ = {};
    current_poke_start_time_micros_ = 0;
    return;
  }

  u16 hit_target_id = *maybe_hit_target_id;
  bool is_hitting_current_target =
      current_poke_target_id_.has_value() && *(current_poke_target_id_) == hit_target_id;
  if (!is_hitting_current_target) {
    // Starting time on new target.
    current_poke_target_id_ = hit_target_id;
    current_poke_start_time_micros_ = timer_.GetElapsedMicros();
    return;
  }

  // Still targeting the correct target.
  // Has enough time elapsed to kill target?
  i64 now_micros = timer_.GetElapsedMicros();
  i64 age_micros = now_micros - current_poke_start_time_micros_;
  i64 min_age_micros = SecondsToMicros(def_.shot_type().has_poke_kill_time_seconds()
                                           ? def_.shot_type().poke_kill_time_seconds()
                                           : kPokeBallKillTimeSeconds);
  if (age_micros >= min_age_micros) {
    stats_.num_hits++;
    stats_.num_shots++;
    stats_.num_kills++;
    PlaySound(SoundType::CLICK_KILL);
    data->force_render = true;

    auto hit_target_id = *maybe_hit_target_id;
    AddNewTarget(hit_target_id);

    if (replay_) {
      replay_->AddMouseClick(timer_.GetElapsedMicros(), true);
    }

    current_poke_target_id_ = {};
    current_poke_start_time_micros_ = 0;
  }
}

void BaseScenario::HandleClickHits(UpdateStateData* data) {
  if (!data->has_click) {
    return;
  }

  i64 now_micros = timer_.GetElapsedMicros();
  i64 time_since_last_shot = now_micros - last_shot_time_micros_;

  // Has available reload shot?
  if (def_.shot_type().has_reload()) {
    i64 reload_rate_micros = SecondsToMicros(def_.shot_type().reload().reload_time());
    bool reload_on_time = time_since_last_shot >= reload_rate_micros;
    if (reload_on_time) {
      available_shots_ = def_.shot_type().reload().max_shots();
    }
    if (available_shots_ <= 0) {
      PlaySound(SoundType::RELOAD);
      return;
    }
  }

  if (ShouldLimitShotRate()) {
    // See if we should allow the shot.
    i64 shot_rate_micros = SecondsToMicros(def_.shot_type().click_rate_seconds());
    if (time_since_last_shot < shot_rate_micros) {
      // Play sound?
      return;
    }
    // Allow the shot.
  }

  last_shot_time_micros_ = now_micros;
  stats_.num_shots++;
  auto maybe_hit_target_id = target_manager_.GetNearestHitTarget(camera_, look_at_.front);
  if (replay_) {
    replay_->AddMouseClick(timer_.GetElapsedMicros(), maybe_hit_target_id.has_value());
  }

  bool is_hit = maybe_hit_target_id.has_value();
  if (!is_hit) {
    PlaySound(SoundType::CLICK_MISS);
    // Missed shot
    if (def_.shot_type().remove_closest_on_miss()) {
      // TODO(mjohns): Count partial kill for multi click?
      std::optional<u16> target_id_to_remove =
          target_manager_.GetNearestTargetOnMiss(camera_, look_at_.front);
      if (target_id_to_remove.has_value()) {
        AddNewTarget(*target_id_to_remove);
      }
    }
    if (def_.shot_type().has_ghost_closest_on_miss()) {
      std::optional<u16> target_id_to_ghost =
          target_manager_.GetNearestTargetOnMiss(camera_, look_at_.front);
      if (target_id_to_ghost.has_value()) {
        float time_to_wait = def_.shot_type().ghost_closest_on_miss();
        bool unghost = def_.shot_type().unghost_miss_on_expiration();
        if (time_to_wait > 0) {
          target_manager_.GetMutableTarget(*target_id_to_ghost)->is_ghost = true;
          RunAfterSeconds(time_to_wait, [=, this]() {
            Target* target_to_ghost = target_manager_.GetMutableTarget(*target_id_to_ghost);
            // Make sure the target wasn't remove while we waited.
            if (target_to_ghost != nullptr) {
              if (unghost) {
                target_to_ghost->is_ghost = false;
              } else {
                AddNewTarget(*target_id_to_ghost);
              }
            }
          });
        } else {
          if (!unghost) {
            AddNewTarget(*target_id_to_ghost);
          }
        }
      }
    }

    if (def_.shot_type().has_reload()) {
      available_shots_ = std::max<int>(0, available_shots_ - 1);
    }
    return;
  }

  // Shot is a hit.
  stats_.num_hits++;

  if (def_.shot_type().has_reload()) {
    available_shots_ =
        std::min<int>(def_.shot_type().reload().max_shots(),
                      available_shots_ + def_.shot_type().reload().num_to_reload_on_hit());
  }

  bool is_kill = true;

  if (GetShotType() == ShotType::kClickMulti) {
    Target* hit_target = target_manager_.GetMutableTarget(*maybe_hit_target_id);
    hit_target->click_count++;
    PlaySound(SoundType::CLICK_HIT);
    if (hit_target->radius_at_kill.has_value()) {
      float radius_diff =
          hit_target->radius_at_kill->start_radius - hit_target->radius_at_kill->end_radius;
      float health_percent = hit_target->GetHealthPercent();
      hit_target->radius = hit_target->radius_at_kill->end_radius + health_percent * radius_diff;
    }

    is_kill = hit_target->click_count >= hit_target->health_clicks;
  }

  if (is_kill) {
    stats_.num_kills++;
    data->force_render = true;
    auto hit_target_id = *maybe_hit_target_id;
    AddNewTarget(hit_target_id);
    PlaySound(SoundType::CLICK_KILL);
  } else {
    PlaySound(SoundType::CLICK_HIT);
  }
}

void BaseScenario::DrawAdditionalUiElements() {
  if (ShouldLimitShotRate()) {
    // See if a shot would be allowed
    i64 shot_rate_micros = SecondsToMicros(def_.shot_type().click_rate_seconds());
    i64 now_micros = timer_.GetElapsedMicros();
    i64 time_since_last_shot = now_micros - last_shot_time_micros_;
    if (time_since_last_shot < shot_rate_micros) {
      // Can't shoot. Draw UI element to show remaining time.
      float time_remaining_percent =
          (shot_rate_micros - time_since_last_shot) / static_cast<float>(shot_rate_micros);
      ScreenInfo screen_info = app_.screen_info();

      float char_x = ImGui::GetDefaultCharSizeX();
      float width = char_x * 20;

      float left_width = width * (1 - time_remaining_percent);

      float y_min = char_x * 2;
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
  }
}

Target BaseScenario::GetNewTarget() {
  Target t = GetTargetTemplate(GetNextTargetProfile());
  FillInNewTarget(&t);
  return t;
}

void BaseScenario::AddNewTarget(u16 old_target_id, bool is_init) {
  if (old_target_id > 0) {
    AddRemoveTargetEvent(old_target_id);
    bool removed = target_manager_.RemoveTarget(old_target_id);
    assert(removed && "Trying to remove invalid target");
  }

  Target target = GetNewTarget();
  if (def_.target_def().newest_target_is_ghost()) {
    target_manager_.MarkAllAsNonGhost();
    target.is_ghost = true;
  }

  if (!is_init && def_.target_def().new_target_delay_seconds() > 0) {
    RunAfterSeconds(def_.target_def().new_target_delay_seconds(), [=, this]() {
      Target new_target = target;
      if (new_target.growth_info) {
        new_target.growth_info->start_time_seconds = timer_.GetElapsedSeconds();
      }
      if (def_.target_def().remove_target_after_seconds() > 0) {
        new_target.remove_after_time_seconds =
            timer_.GetElapsedSeconds() + def_.target_def().remove_target_after_seconds();
      }
      // Make sure to remark at the time the target is getting added too.
      if (def_.target_def().newest_target_is_ghost()) {
        target_manager_.MarkAllAsNonGhost();
      }
      new_target = target_manager_.AddTarget(new_target);
      AddNewTargetEvent(new_target);
    });
  } else {
    if (def_.target_def().remove_target_after_seconds() > 0) {
      target.remove_after_time_seconds =
          timer_.GetElapsedSeconds() + def_.target_def().remove_target_after_seconds();
    }
    target = target_manager_.AddTarget(target);
    AddNewTargetEvent(target);
  }
}

std::optional<StatsDbRow> BaseScenario::GetStatsRow() {
  ShotType::TypeCase shot_type = GetShotType();

  float num_hits = stats_.num_hits;
  float num_shots = stats_.num_shots;
  float score = CalculateScore(def_.duration_seconds());

  std::optional<ProximityPercentiles> maybe_proximity_percentiles;
  switch (shot_type) {
    case ShotType::kTrackingInvincible: {
      num_hits = stats_.hit_stopwatch.GetElapsedSeconds();
      num_shots = def_.duration_seconds();
      break;
    }
    case ShotType::kTrackingProximity: {
      float total_micros = SecondsToMicros(def_.duration_seconds());
      num_shots = def_.duration_seconds();
      num_hits = (stats_.proximity.hit_micros_map[100] / total_micros) * num_shots;

      ProximityPercentiles p;
      auto get_percent = [&](int percent) {
        return 100 * (stats_.proximity.hit_micros_map[percent] / (float)total_micros);
      };
      p.set_p90(get_percent(90));
      p.set_p80(get_percent(80));
      p.set_p70(get_percent(70));
      p.set_p60(get_percent(60));
      p.set_p50(get_percent(50));
      p.set_p40(get_percent(40));
      p.set_p30(get_percent(30));
      p.set_p20(get_percent(20));
      p.set_p10(get_percent(10));
      maybe_proximity_percentiles = p;
      break;
    }
    case ShotType::kTrackingKill: {
      num_hits = stats_.hit_stopwatch.GetElapsedSeconds();
      num_shots = stats_.shot_stopwatch.GetElapsedSeconds();
      break;
    }
    case ShotType::kClickSingle:
    case ShotType::kClickMulti:
    case ShotType::kPoke:
    case ShotType::kPokeInstant:
      // Nothing extra to do
      break;
    default:
      break;
  }

  StatsDbRow stats_row;
  stats_row.mm_per_360 = effective_cm_per_360_ * 10;
  if (num_hits > 0) {
    stats_row.info.set_num_hits(num_hits);
  }
  if (num_shots > 0) {
    stats_row.info.set_num_shots(num_shots);
  }
  stats_row.score = score;
  if (maybe_proximity_percentiles) {
    *stats_row.info.mutable_proximity_percentiles() = *maybe_proximity_percentiles;
  }
  return stats_row;
}

float BaseScenario::CalculateScore(float current_time) {
  ShotType::TypeCase shot_type = GetShotType();
  if (current_time <= 0) {
    return 0;
  }
  float time_normalized_multiplier = 60.0f / current_time;
  switch (shot_type) {
    case ShotType::kTrackingInvincible: {
      float hit_percent = stats_.hit_stopwatch.GetElapsedSeconds() / current_time;
      return 100 * hit_percent;
    }
    case ShotType::kTrackingProximity:
    case ShotType::kTrackingKill: {
      return stats_.num_hits * time_normalized_multiplier;
    }
    case ShotType::kClickSingle:
    case ShotType::kClickMulti: {
      // Default clicking scoring
      if (stats_.num_shots <= 0) {
        return 0;
      }
      if (def_.shot_type().accuracy_penalty() == AccuracyPenalty::ACCURACY_PENALTY_NONE) {
        return stats_.num_hits * time_normalized_multiplier;
      }
      float hit_percent = stats_.num_hits / stats_.num_shots;
      // float duration_modifier = 60.0f / def_.duration_seconds();
      float accuracy_penalty = 1.0 - sqrt(hit_percent);
      return stats_.num_hits * (1 - accuracy_penalty) * time_normalized_multiplier;
    }
    case ShotType::kPoke:
    case ShotType::kPokeInstant:
      return stats_.num_kills * time_normalized_multiplier;
    default:
      break;
  }
  return 0;
}

}  // namespace aim
