#include <memory>

#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/profile_selection.h"
#include "aim/core/target.h"
#include "aim/scenario/base_scenario.h"
#include "aim/scenario/scenario.h"
#include "aim/scenario/strafe_controller.h"
#include "aim/scenario/target_placement.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {
namespace {

class BounceController {
 public:
  BounceController(float min_y, float max_y, float unscaled_acceleration)
      : min_y_(min_y), max_y_(max_y), unscaled_acceleration_(unscaled_acceleration) {}

  float GetUpdatedHeight(Target& t,
                         float y,
                         const Room& room,
                         const Wall& wall,
                         const BounceScenarioDef& def,
                         Random& rand,
                         float now_seconds,
                         float delta_seconds) {
    float actual_min = min_y_ + t.radius;
    if (!initialized_) {
      initialized_ = true;
      unscaled_max_speed_ = t.speed;
      StartNewBounce(y, def, wall, rand, now_seconds, actual_min);
    }

    if (wait_until_time_ > 0) {
      if (wait_until_time_ > now_seconds) {
        return y;
      }
      wait_until_time_ = -1;
    }

    if (!going_up_ && y <= actual_min) {
      StartNewBounce(y, def, wall, rand, now_seconds, actual_min);
      return actual_min;
    }

    float actual_max = max_y_ - t.radius;
    float target_y = min_y_ + height_;
    if (target_y > actual_max) {
      target_y = actual_max;
    }
    if (going_up_) {
      bool too_high = y >= target_y;
      bool stopped = current_speed_ <= 0.001 && is_stopping_;
      if (too_high || stopped) {
        going_up_ = false;
        max_speed_ = down_max_speed_;
        acceleration_ = down_acceleration_;
        is_stopping_ = false;
        if (acceleration_ > 0) {
          current_speed_ = 0;
        }
        if (float_time_ > 0) {
          wait_until_time_ = now_seconds + float_time_;
        }

        return y;
      }
    }

    if (going_up_) {
      if (!is_stopping_ && acceleration_ > 0) {
        // See if we should start stopping.
        float stop_distance = GetStopDistance(current_speed_, acceleration_);
        bool should_stop = y + stop_distance > target_y;
        if (should_stop) {
          is_stopping_ = true;
        }
      }
    }

    if (acceleration_ > 0) {
      if (is_stopping_) {
        current_speed_ -= delta_seconds * acceleration_;
        if (current_speed_ < 0) {
          current_speed_ = 0;
        }
      } else {
        current_speed_ += delta_seconds * acceleration_;
        if (current_speed_ > max_speed_) {
          current_speed_ = max_speed_;
        }
      }
    } else {
      current_speed_ = max_speed_;
    }

    float new_y = y;
    if (going_up_) {
      new_y += delta_seconds * current_speed_;
    } else {
      new_y -= delta_seconds * current_speed_;
    }

    return new_y;
  }

 private:
  void StartNewBounce(float y,
                      const BounceScenarioDef& def,
                      const Wall& wall,
                      Random& rand,
                      float now_seconds,
                      float actual_min) {
    bounce_number_++;
    BounceProfile profile = GetNextProfile(def, rand);
    going_up_ = true;
    wait_until_time_ = -1;
    float_time_ = rand.GetJittered(profile.float_time(), profile.float_time_jitter());

    float delay = rand.GetJittered(profile.delay_seconds(), profile.delay_seconds_jitter());
    if (delay > 0) {
      bool can_delay = true;
      if (profile.only_delay_on_floor()) {
        bool is_on_floor = y <= (actual_min + 0.2);
        can_delay = is_on_floor;
      }
      if (can_delay) {
        wait_until_time_ = now_seconds + delay;
      }
    }

    height_ = rand.GetJittered(wall.GetRegionLength(profile.height()),
                               wall.GetRegionLength(profile.height_jitter()));

    max_speed_ = unscaled_max_speed_;
    if (profile.has_speed_multiplier()) {
      max_speed_ *= profile.speed_multiplier();
    }
    down_max_speed_ = max_speed_;
    if (profile.has_down_speed_multiplier()) {
      down_max_speed_ *= profile.down_speed_multiplier();
    }

    acceleration_ = unscaled_acceleration_;
    if (profile.has_acceleration_multiplier()) {
      acceleration_ *= profile.acceleration_multiplier();
    }
    down_acceleration_ = acceleration_;
    if (profile.has_down_acceleration_multiplier()) {
      down_acceleration_ *= profile.down_acceleration_multiplier();
    }

    float target_y = min_y_ + height_;
    if (y >= target_y) {
      going_up_ = false;
      current_speed_ = 0;
      max_speed_ = down_max_speed_;
      acceleration_ = down_acceleration_;
      /*
      if (bounce_number_ == 1) {
        current_speed_ =
            std::min<float>(max_speed_, GetStartSpeedForStopDistance(y - min_y_, acceleration_));
      }
      */
      return;
    }

    current_speed_ =
        std::min<float>(max_speed_, GetStartSpeedForStopDistance(target_y - y, acceleration_));
  }

  BounceProfile GetNextProfile(const BounceScenarioDef& def, Random& rand) {
    auto p =
        SelectProfile(def.bounce_profiles_info(), def.bounce_profiles(), &selection_context_, rand);
    if (!p.has_value()) {
      BounceProfile profile;
      profile.set_delay_seconds(1000);
      return profile;
    }
    return *p;
  }

  float min_y_;
  float max_y_;
  bool initialized_ = false;

  float height_;
  bool going_up_ = true;
  bool is_stopping_ = false;

  float current_speed_;

  float acceleration_;
  float max_speed_;

  float down_acceleration_;
  float down_max_speed_;

  float wait_until_time_ = -1;

  float unscaled_max_speed_;
  float unscaled_acceleration_;

  float float_time_ = 0;

  int bounce_number_ = 0;
  ProfileSelectionContext selection_context_{};
};

class MovementControllerImpl : public MovementController {
 public:
  MovementControllerImpl(
      float speed, float acceleration, Wall wall, float min_y, ScenarioDef def, Application& app)
      : wall_(wall), def_(def), app_(app) {
    auto d = def_.bounce_def();
    const WallBounds bounds = wall.GetWallBounds(d.bounds());
    const WallRelativeBounds relative_bounds = wall.GetWallRelativeBounds(d.relative_bounds());
    const WallBounds absolute_bounds = wall.GetAbsoluteWallBounds();
    bounce_controller_ =
        std::make_unique<BounceController>(min_y, wall.height / 2.0f, acceleration);

    DirectionParams params;
    params.acceleration = acceleration;

    if (d.left_right_profiles_size() > 0) {
      left_right_controller_ = StrafeController(bounds.min_x,
                                                bounds.max_x,
                                                relative_bounds.min_x,
                                                relative_bounds.max_x,
                                                absolute_bounds.min_x,
                                                absolute_bounds.max_x,
                                                d.left_right_initial_direction(),
                                                params,
                                                wall);
    }

    if (d.forward_back_profiles_size() > 0) {
      forward_back_controller_ = StrafeController(bounds.min_depth,
                                                  bounds.max_depth,
                                                  relative_bounds.min_depth,
                                                  relative_bounds.max_depth,
                                                  absolute_bounds.min_depth,
                                                  absolute_bounds.max_depth,
                                                  d.forward_back_initial_direction(),
                                                  params,
                                                  wall);
    }
  }

  void UpdatePosition(float now_seconds,
                      Target& t,
                      const Room& room,
                      float delta_seconds) override {
    if (!t.wall_position.has_value()) {
      t.wall_position = glm::vec2(0.0f);
    }

    glm::vec2& pos = *t.wall_position;

    if (left_right_controller_) {
      pos.x =
          left_right_controller_->GetUpdatedPosition(t,
                                                     app_.rand(),
                                                     def_.bounce_def().left_right_profiles(),
                                                     def_.bounce_def().left_right_profiles_info(),
                                                     pos.x,
                                                     now_seconds,
                                                     delta_seconds,
                                                     /*is_left_right=*/true);
    }

    if (forward_back_controller_) {
      t.wall_depth = forward_back_controller_->GetUpdatedPosition(
          t,
          app_.rand(),
          def_.bounce_def().forward_back_profiles(),
          def_.bounce_def().forward_back_profiles_info(),
          t.wall_depth,
          now_seconds,
          delta_seconds);
    }

    if (bounce_controller_) {
      float updated_y = bounce_controller_->GetUpdatedHeight(
          t, pos.y, room, wall_, def_.bounce_def(), app_.rand(), now_seconds, delta_seconds);
      pos.y = updated_y;
    }

    t.position = WallPositionToWorldPosition(*t.wall_position, t.radius, room, t.wall_depth);
  }

 private:
  Wall wall_;
  ScenarioDef def_;
  Application& app_;

  std::optional<StrafeController> left_right_controller_;
  std::optional<StrafeController> forward_back_controller_;
  std::unique_ptr<BounceController> bounce_controller_;
};

class BounceScenario : public BaseScenario {
 public:
  explicit BounceScenario(const CreateScenarioParams& params)
      : BaseScenario(params), wall_(Wall::ForRoom(params.def.room())) {
    if (def_.bounce_def().has_target_placement_strategy()) {
      wall_target_placer_ = CreateWallTargetPlacer(
          wall_, def_.bounce_def().target_placement_strategy(), &target_manager_, &app_);
    }
  }

 protected:
  ShotType::TypeCase GetDefaultShotType() override {
    return ShotType::kTrackingInvincible;
  }

  void FillInNewTarget(Target* target) override {
    glm::vec3 pos(0.0f);
    if (wall_target_placer_) {
      pos = wall_target_placer_->GetNextPosition();
    } else {
      if (def_.bounce_def().forward_back_profiles_size() > 0) {
        float depth = wall_.GetWallBounds(def_.bounce_def().bounds()).max_depth;
        // Start in the middle of the available depth.
        pos.z = depth / 2.0;
      }
    }

    auto d = def_.bounce_def();
    float min_y = -1 * (wall_.height / 2.0);
    if (d.has_floor_height()) {
      min_y += wall_.GetRegionLength(d.floor_height());
    }

    if (d.start_on_floor()) {
      pos.y = min_y + target->radius;
    }

    target->SetWallPosition(pos, def_.room());
    target->movement_controller = std::make_shared<MovementControllerImpl>(
        target->speed, target->acceleration, wall_, min_y, def_, app_);
  }

  void UpdateTargetPositions() override {
    target_manager_.UpdateTargetPositions(timer_.GetElapsedSeconds());
  }

 private:
  Wall wall_;
  std::unique_ptr<WallTargetPlacer> wall_target_placer_;
};

}  // namespace

std::unique_ptr<Scenario> CreateBounceScenario(const CreateScenarioParams& params) {
  return std::make_unique<BounceScenario>(params);
}

}  // namespace aim
