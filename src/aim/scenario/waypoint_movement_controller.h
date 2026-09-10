#pragma once

#include <memory>

#include "aim/core/target.h"
#include "aim/scenario/target_placement.h"
#include "glm/vec3.hpp"  // IWYU pragma: keep

namespace aim {

class WallWaypointSupplier {
 public:
  WallWaypointSupplier() {}
  virtual ~WallWaypointSupplier() {}

  virtual glm::vec3 GetNextPosition() = 0;
};

class WallTargetPlacerWaypointSupplier : public WallWaypointSupplier {
 public:
  WallTargetPlacerWaypointSupplier(glm::vec3 first_point,
                                   std::unique_ptr<WallTargetPlacer> wall_target_placer)
      : first_point_(first_point), wall_target_placer_(std::move(wall_target_placer)) {}

  glm::vec3 GetNextPosition() override {
    if (is_first_) {
      is_first_ = false;
      return first_point_;
    }
    return wall_target_placer_->GetNextPosition();
  }

 private:
  bool is_first_ = true;
  glm::vec3 first_point_;
  std::unique_ptr<WallTargetPlacer> wall_target_placer_;
};

std::shared_ptr<MovementController> CreateWallWaypointMovementController(
    float speed, float acceleration, std::unique_ptr<WallWaypointSupplier> waypoint_supplier);

}  // namespace aim
