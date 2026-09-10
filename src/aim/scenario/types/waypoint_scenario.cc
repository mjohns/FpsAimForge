#include <memory>

#include "aim/core/target.h"
#include "aim/scenario/base_scenario.h"
#include "aim/scenario/scenario.h"
#include "aim/scenario/target_placement.h"
#include "aim/scenario/waypoint_movement_controller.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {
namespace {

class WaypointScenario : public BaseScenario {
 public:
  explicit WaypointScenario(const CreateScenarioParams& params)
      : BaseScenario(params), wall_(Wall::ForRoom(params.def.room())) {
    if (def_.target_def().num_targets() == 1 && !def_.waypoint_def().start_in_center()) {
      // Initialize the camera to look at the initial target.
      wall_target_placer_ = GetNewPlacer();
      initial_position_ = wall_target_placer_->GetNextPosition();

      float target_radius = GetNextTargetProfile().target_radius();
      glm::vec3 look_at_pos =
          WallPositionToWorldPosition(initial_position_, target_radius, params.def.room());
      camera_.SetPitchYawLookingAtPoint(look_at_pos);
    }
  }

 protected:
  ShotType::TypeCase GetDefaultShotType() override {
    return ShotType::kTrackingInvincible;
  }

  void FillInNewTarget(Target* target) override {
    glm::vec3 pos(0.0f);
    std::unique_ptr<WallTargetPlacer> placer;
    if (wall_target_placer_) {
      // Initial position was created in constructor already.
      pos = initial_position_;
      placer = std::move(wall_target_placer_);
    } else {
      placer = GetNewPlacer();
      if (def_.waypoint_def().start_in_center()) {
        pos = glm::vec3(0.0f);
      } else {
        pos = placer->GetNextPosition();
      }
    }
    target->SetWallPosition(pos, def_.room());

    auto waypoint_supplier =
        std::make_unique<WallTargetPlacerWaypointSupplier>(pos, std::move(placer));
    target->movement_controller = CreateWallWaypointMovementController(
        target->speed, target->acceleration, std::move(waypoint_supplier));
  }

  void UpdateTargetPositions() override {
    target_manager_.UpdateTargetPositions(timer_.GetElapsedSeconds());
  }

 private:
  std::unique_ptr<WallTargetPlacer> GetNewPlacer() {
    return CreateWallTargetPlacer(
        wall_, def_.waypoint_def().target_placement_strategy(), &target_manager_, &app_);
  }

  std::unique_ptr<WallTargetPlacer> wall_target_placer_;
  glm::vec3 initial_position_{0.0f};
  Wall wall_;
};

}  // namespace

std::unique_ptr<Scenario> CreateWaypointScenario(const CreateScenarioParams& params) {
  return std::make_unique<WaypointScenario>(params);
}

}  // namespace aim
