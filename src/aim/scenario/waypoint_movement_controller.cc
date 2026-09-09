#include "waypoint_movement_controller.h"

#include "aim/common/util.h"
#include "aim/core/target.h"
#include "aim/scenario/basic_movement_controller.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {

namespace {

class WallWaypointMovementController : public WallDepthMovementController {
 public:
  WallWaypointMovementController(float speed,
                                 float acceleration,
                                 std::unique_ptr<WallWaypointSupplier> waypoint_supplier)
      : WallDepthMovementController(speed),
        waypoint_supplier_(std::move(waypoint_supplier)),
        acceleration_(acceleration) {}

 protected:
  void UpdateDirectionAndSpeed(Target& t, float delta_seconds) override {
    glm::vec3 pos = t.GetWallPosition3();

    if (!initialized_) {
      initialized_ = true;
      StartMovingToNextWaypoint(pos);
    }

    float distance_traveled = glm::length(pos - current_start_);
    float distance_left = current_distance_to_travel_ - distance_traveled;
    if (distance_left <= 0 || (is_stopping_ && speed_ <= 0)) {
      StartMovingToNextWaypoint(pos);
      return;
    }

    if (acceleration_ <= 0) {
      // No acceratation so no need to update the speed.
      return;
    }

    // Adjust the speed as necessary based on acceleration.

    if (!is_stopping_) {
      float stop_distance = GetStopDistance(speed_, acceleration_);
      if (stop_distance >= distance_left) {
        is_stopping_ = true;
      }
    }

    if (is_stopping_) {
      speed_ -= (delta_seconds * acceleration_);
      return;
    }

    // Accelerate to max speed.
    if (speed_ < original_speed_) {
      speed_ += (delta_seconds * acceleration_);
      speed_ = std::min(original_speed_, speed_);
    }
  }

 private:
  void StartMovingToNextWaypoint(const glm::vec3& pos) {
    glm::vec3 next_pos = waypoint_supplier_->GetNextPosition();

    current_distance_to_travel_ = glm::length(next_pos - pos);

    // Direction should also contain depth direction.
    direction_ = glm::normalize(next_pos - pos);
    current_start_ = pos;
    is_stopping_ = false;

    if (acceleration_ > 0) {
      speed_ = 0;
    }
  }

  std::unique_ptr<WallWaypointSupplier> waypoint_supplier_;

  float current_distance_to_travel_ = 0;
  glm::vec3 current_start_{};

  bool initialized_ = false;
  float acceleration_;
  bool is_stopping_ = false;
};

}  // namespace

std::shared_ptr<MovementController> CreateWallWaypointMovementController(
    float speed, float acceleration, std::unique_ptr<WallWaypointSupplier> waypoint_supplier) {
  return std::make_shared<WallWaypointMovementController>(
      speed, acceleration, std::move(waypoint_supplier));
}

}  // namespace aim
