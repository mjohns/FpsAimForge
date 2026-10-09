#include <memory>

#include "aim/common/geometry.h"
#include "aim/core/application.h"
#include "aim/core/target.h"
#include "aim/scenario/base_scenario.h"
#include "aim/scenario/scenario.h"
#include "aim/scenario/waypoint_movement_controller.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {
namespace {

class CenteringWaypointSupplier : public WallWaypointSupplier {
 public:
  explicit CenteringWaypointSupplier(const glm::vec2& starting_point,
                                     const std::vector<glm::vec2>& points)
      : starting_point_(starting_point), points_(points) {}

  glm::vec3 GetNextPosition() override {
    glm::vec2 point{};
    if (counter_ == 0) {
      point = starting_point_;
    } else {
      if (points_.size() > 0) {
        int i = counter_ % points_.size();
        point = points_[i];
      }
    }
    counter_++;
    return glm::vec3(point.x, point.y, 0);
  }

 private:
  int counter_ = 0;
  glm::vec2 starting_point_;
  std::vector<glm::vec2> points_;
};

class CenteringScenario : public BaseScenario {
 public:
  explicit CenteringScenario(const CreateScenarioParams& params)
      : BaseScenario(params), wall_(Wall::ForRoom(params.def.room())) {
    const CenteringScenarioDef& c = def_.centering_def();

    if (c.has_angle()) {
      glm::vec2 basis(wall_.GetRegionLength(c.angle_length()) / 2.0, 0);
      float angle = app_.rand().GetJittered(c.angle(), c.angle_jitter());
      wall_points_.push_back(RotateDegrees(basis, angle + 180));
      wall_points_.push_back(RotateDegrees(basis, angle));
    } else {
      for (const auto& p : c.wall_points()) {
        wall_points_.push_back(wall_.GetRegionVec2(p));
      }
    }

    if (wall_points_.size() > 1) {
      glm::vec2 start = wall_points_[0];
      glm::vec2 end = wall_points_[1];

      glm::vec2 direction = end - start;

      // Start halfway across
      direction = direction * 0.5f;
      start_point_ = end - direction;

      // Look a little in front of the starting position
      float target_radius = GetNextTargetProfile().target_radius();
      glm::vec3 look_at_pos = WallPositionToWorldPosition(
          start_point_ + (glm::normalize(direction) * 2.0f * target_radius),
          // start_point_,
          target_radius,
          params.def.room());
      camera_.SetPitchYawLookingAtPoint(look_at_pos);
    }
  }

 protected:
  ShotType::TypeCase GetDefaultShotType() override {
    return ShotType::kTrackingInvincible;
  }

  void FillInNewTarget(Target* target) override {
    glm::vec3 pos3(start_point_.x, start_point_.y, 0);
    target->SetWallPosition(pos3, def_.room());
    auto waypoint_supplier =
        std::make_unique<CenteringWaypointSupplier>(start_point_, wall_points_);
    target->movement_controller = CreateWallWaypointMovementController(
        target->speed, target->acceleration, wall_, std::move(waypoint_supplier));
  }

  void UpdateTargetPositions() override {
    target_manager_.UpdateTargetPositions(timer_.GetElapsedSeconds());
  }

 private:
  glm::vec2 start_point_;
  std::vector<glm::vec2> wall_points_;
  Wall wall_;
};

}  // namespace

std::unique_ptr<Scenario> CreateCenteringScenario(const CreateScenarioParams& params) {
  return std::make_unique<CenteringScenario>(params);
}

}  // namespace aim
