#include <memory>

#include "aim/common/util.h"
#include "aim/common/wall.h"
#include "aim/core/application.h"
#include "aim/core/camera.h"
#include "aim/scenario/base_scenario.h"
#include "aim/scenario/scenario.h"
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {
namespace {

class WallArcScenario : public BaseScenario {
 public:
  explicit WallArcScenario(const CreateScenarioParams& params)
      : BaseScenario(params), wall_(Wall::ForRoom(params.def.room())) {
    auto arc = params.def.wall_arc_def();
    width_ = wall_.GetRegionLength(arc.width());
    height_ = wall_.GetRegionLength(arc.height());

    float target_radius = GetNextTargetProfile().target_radius();
    float start_height_mult =
        arc.start_on_ground() ? (wall_.height - (target_radius * 2)) : height_;
    if (arc.reflect()) {
      wall_start_.y = 0.5 * start_height_mult;
    } else {
      wall_start_.y = -0.5 * start_height_mult;
    }

    wall_start_.x = -0.5 * width_;

    // Look at start position
    glm::vec3 look_at_pos = WallPositionToWorldPosition(wall_start_, target_radius, def_.room());
    camera_.SetPitchYawLookingAtPoint(look_at_pos);
  }

 protected:
  ShotType::TypeCase GetDefaultShotType() override {
    return ShotType::kTrackingInvincible;
  }

  void FillInNewTarget(Target* target) override {
    target->wall_position = wall_start_;
  }

  void UpdateTargetPositions() override {
    // Determine if the target needs to change direction
    Target* target = nullptr;
    for (Target* t : target_manager_.GetMutableVisibleTargets()) {
      target = t;
      break;
    }

    if (target == nullptr) {
      return;
    }

    float now_seconds = timer_.GetElapsedSeconds();

    if (now_seconds >= bounce_end_time_) {
      StartNewBounce(target->speed, target->acceleration);
    }

    float time_from_start = now_seconds - bounce_start_time_;

    float bounce_duration = bounce_end_time_ - bounce_start_time_;
    float bounce_mid_time = bounce_start_time_ + bounce_duration * 0.5;

    float percent_across = time_from_start / bounce_duration;
    float x = going_right_ ? (percent_across - 0.5f) * width_ : (0.5 - percent_across) * width_;

    bool going_down = now_seconds > bounce_mid_time;
    bool going_up = !going_down;

    now_seconds = timer_.GetElapsedSeconds();
    float delta_seconds = now_seconds - last_update_time_;
    last_update_time_ = now_seconds;

    if (going_up) {
      float distance_left = height_ - current_y_;
      if (!is_stopping_) {
        // See if we need to start decelerating.
        float stop_time = GetStopTime(current_speed_, acceleration_);
        float time_left = bounce_mid_time - now_seconds;
        if (stop_time >= time_left) {
          is_stopping_ = true;
        }
      }

      if (is_stopping_) {
        current_speed_ -= delta_seconds * acceleration_;
        if (current_speed_ < 0) {
          current_speed_ = 0;
        }
      }

      current_y_ += current_speed_ * delta_seconds;
    } else {
      // Going down. Accelerate to max speed.
      current_speed_ += delta_seconds * acceleration_;
      if (current_speed_ > max_speed_) {
        current_speed_ = max_speed_;
      }

      current_y_ -= current_speed_ * delta_seconds;
    }

    current_y_ = std::clamp(current_y_, 0.0f, height_);

    // Adjust the y to the current location on the wall possibly reflecting.
    float y = current_y_;
    if (def_.wall_arc_def().reflect()) {
      y = wall_start_.y - y;
    } else {
      y = wall_start_.y + y;
    }

    glm::vec2 wall_point;
    wall_point.x = x;
    wall_point.y = y;
    target->wall_position = wall_point;
    target->position = WallPositionToWorldPosition(wall_point, target->radius, def_.room());
  }

 private:
  void StartNewBounce(float speed, float acceleration) {
    height_ = app_.rand().GetJittered(wall_.GetRegionLength(def_.wall_arc_def().height()),
                                      wall_.GetRegionLength(def_.wall_arc_def().height_jitter()));
    acceleration_ = acceleration;
    if (acceleration_ <= 0) {
      acceleration_ = 1000000;
    }

    max_speed_ = speed;
    current_speed_ = std::min(speed, GetStartSpeedForStopDistance(height_, acceleration_));
    bounce_start_time_ = timer_.GetElapsedSeconds();

    float bounce_time = GetStopTime(current_speed_, acceleration_);
    float stop_distance = GetStopDistance(current_speed_, acceleration_);

    float remaining_height_at_max_speed = height_ - stop_distance;
    if (remaining_height_at_max_speed > 0) {
      bounce_time += remaining_height_at_max_speed / current_speed_;
    }

    bounce_end_time_ = bounce_start_time_ + 2.0f * bounce_time;
    going_right_ = !going_right_;
    current_y_ = 0;
    is_stopping_ = false;
    last_update_time_ = bounce_start_time_;
  }

  Wall wall_;
  float width_;

  float acceleration_;

  float current_speed_;
  float max_speed_;

  float current_y_;

  float bounce_start_time_ = -1;
  float height_ = 0;
  float bounce_end_time_ = -1;
  bool going_right_ = false;
  bool is_stopping_ = false;
  float last_update_time_ = 0;

  glm::vec2 wall_start_;
};

}  // namespace

std::unique_ptr<Scenario> CreateWallArcScenario(const CreateScenarioParams& params) {
  return std::make_unique<WallArcScenario>(params);
}

}  // namespace aim
