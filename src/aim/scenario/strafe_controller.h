#pragma once

#include <memory>

#include "aim/common/random.h"
#include "aim/common/wall.h"
#include "aim/core/application.h"
#include "aim/core/target.h"
#include "aim/proto/scenario.pb.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {

struct DirectionParams {
  float acceleration = 0;
};

class StrafeController {
 public:
  StrafeController(float min,
                   float max,
                   std::optional<float> relative_min,
                   std::optional<float> relative_max,
                   float absolute_min,
                   float absolute_max,
                   Direction initial_direction,
                   DirectionParams params,
                   Wall wall)
      : initial_direction_(initial_direction),
        min_(min),
        max_(max),
        relative_min_(relative_min),
        relative_max_(relative_max),
        absolute_min_(absolute_min),
        absolute_max_(absolute_max),
        unscaled_acceleration_(params.acceleration),
        absolute_center_((min + max) / 2.0f),
        wall_(wall) {}

  float GetUpdatedPosition(Target& t,
                           Random& rand,
                           const google::protobuf::RepeatedPtrField<StrafeProfile>& profiles,
                           const ProfileListInfo& profiles_info,
                           float current_position,
                           float now_seconds,
                           float delta_seconds,
                           bool is_left_right = false);

 private:
  bool GetInitialGoingLeft(Direction dir, float current_position, Random& rand);

  void ChangeDirection(Random& rand,
                       float now_seconds,
                       const google::protobuf::RepeatedPtrField<StrafeProfile>& profiles,
                       const ProfileListInfo& profiles_info,
                       float target_speed,
                       float current_position,
                       bool is_first,
                       float* target_radius_out);

  Direction initial_direction_;
  float min_;
  float max_;
  std::optional<float> relative_min_;
  std::optional<float> relative_max_;
  float absolute_min_;
  float absolute_max_;
  float unscaled_acceleration_;
  float absolute_center_;
  float center_ = 0;

  float current_speed_ = 0;
  bool initialized_ = false;

  float next_direction_change_time_ = -1;
  std::optional<float> next_direction_change_pos_;

  float speed_multiplier_ = 1;
  float acceleration_multiplier_ = 1;
  ProfileSelectionContext selection_context_{};
  bool is_stopping_ = false;
  bool going_left_ = false;
  int direction_change_count_ = 0;
  Wall wall_;
  float wait_until_time_ = -1;
  float pause_time_ = -1;
  float original_target_radius_ = -1;
};

std::unique_ptr<MovementController> CreateForwardBackMovementController(
    const Target& target,
    Wall wall,
    const Bounds& bounds_def,
    const google::protobuf::RepeatedPtrField<StrafeProfile>& profiles,
    const ProfileListInfo& profiles_info,
    Direction initial_direction,
    Application& app);

}  // namespace aim
