#include "target_placement.h"

#include <cassert>
#include <optional>

#include "aim/common/geometry.h"
#include "aim/common/util.h"
#include "aim/core/profile_selection.h"
#include "aim/core/scenario_manager.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep

namespace aim {
namespace {

class WallTargetPlacerImpl : public WallTargetPlacer {
 public:
  WallTargetPlacerImpl(const Wall& wall,
                       const TargetPlacementStrategy& strategy,
                       TargetManager* target_manager,
                       Application* app)
      : wall_(wall), strategy_(strategy), target_manager_(target_manager), app_(app) {}

  glm::vec3 GetNextPosition() override {
    ProfileSelectionContext* context = &selection_context_;

    glm::vec3 candidate_pos(0.0f);
    if (strategy_.regions_size() == 0) {
      return candidate_pos;
    }
    float min_distance = wall_.GetRegionLength(strategy_.min_distance());
    float min_x_distance = wall_.GetRegionLength(strategy_.min_x_distance());
    float min_y_distance = wall_.GetRegionLength(strategy_.min_y_distance());
    bool has_min_distance = min_distance > 0 || min_x_distance > 0 || min_y_distance > 0;

    ProfileSelectionContext original_context = *context;
    for (int i = 0; i < 200; ++i) {
      // Reset this so that the counter only increments once even if the candidates are invalid.
      *context = original_context;
      TargetRegion selected_region;
      candidate_pos = GetNewCandidateTargetPosition(context, &selected_region);
      if (selected_region.has_fixed_distance_from_last_target()) {
        // Scale the candidate to the correct distance.
        candidate_pos = glm::vec3(GetFixedDistanceAdjustedPoint(candidate_pos, selected_region),
                                  candidate_pos.z);
      }
      // TODO: consider also checking wall_.IsPointInBounds
      if (!has_min_distance) {
        return candidate_pos;
      }
      if (AreNoneWithinDistanceOnWall(
              candidate_pos, min_distance, min_x_distance, min_y_distance)) {
        return candidate_pos;
      }

      if (i == 100 || i == 150 || i == 175 || i == 190) {
        min_distance *= 0.5;
        min_x_distance *= 0.5;
        min_y_distance *= 0.5;
      }
    }

    if (!logged_cant_place_) {
      logged_cant_place_ = true;
      app_->logger()->warn("Unable to place target in scenario: {}",
                           app_->scenario_manager().GetCurrentScenarioName());
    }
    return candidate_pos;
  }

 private:
  bool AreNoneWithinDistanceOnWall(const glm::vec2& p,
                                   float min_distance,
                                   float min_x_distance,
                                   float min_y_distance) {
    auto is_too_close = [=](const Target& target) {
      if (!target.wall_position) {
        assert(false && "Should not be calling target placement without wall position set");
        return false;
      }
      float distance = glm::length(p - *target.wall_position);
      float actual_min_distance = min_distance > 0 ? min_distance : target.radius * 2;
      if (distance < actual_min_distance) {
        return true;
      }
      if (min_x_distance > 0) {
        float x_distance = std::abs(p.x - target.wall_position->x);
        if (x_distance < min_x_distance) {
          return true;
        }
      }
      if (min_y_distance > 0) {
        float y_distance = std::abs(p.y - target.wall_position->y);
        if (y_distance < min_y_distance) {
          return true;
        }
      }
      return false;
    };
    for (auto& target : target_manager_->GetTargets()) {
      if (target.ShouldDraw()) {
        if (is_too_close(target)) {
          return false;
        }
      }
    }
    const std::optional<Target>& last_target = target_manager_->GetLastRemovedTarget();
    if (last_target && is_too_close(*last_target)) {
      return false;
    }
    return true;
  }

  std::optional<TargetRegion> GetRegionToUse(ProfileSelectionContext* context) {
    return SelectProfile(strategy_.regions_info(), strategy_.regions(), context, app_->rand());
  }

  // Returns an x/z pair where to place the target on the wall.
  glm::vec3 GetNewCandidateTargetPosition(ProfileSelectionContext* context,
                                          TargetRegion* selected_region) {
    auto maybe_region = GetRegionToUse(context);
    if (!maybe_region.has_value()) {
      app_->logger()->warn("Unable to find target region");
      return glm::vec3(0);
    }

    const TargetRegion& region = *maybe_region;
    *selected_region = region;
    float x_offset = wall_.GetRegionLength(region.x_offset());
    float y_offset = wall_.GetRegionLength(region.y_offset());

    float z = ClampPositive(app_->rand().GetJittered(wall_.GetRegionLength(region.depth()),
                                                     wall_.GetRegionLength(region.depth_jitter())));

    if (region.has_point()) {
      glm::vec2 pos;
      pos.x = wall_.GetRegionLength(region.point().x());
      pos.y = wall_.GetRegionLength(region.point().y());
      return glm::vec3(pos, z);
    }

    if (region.has_ellipse()) {
      glm::vec2 pos =
          GetRandomPositionInEllipse(0.5 * wall_.GetRegionLength(region.ellipse().x_diameter()),
                                     0.5 * wall_.GetRegionLength(region.ellipse().y_diameter()),
                                     app_->rand());
      pos.x += x_offset;
      pos.y += y_offset;
      return glm::vec3(pos, z);
    }
    if (region.has_circle()) {
      glm::vec2 pos =
          GetRandomPositionInCircle(0.5 * wall_.GetRegionLength(region.circle().inner_diameter()),
                                    0.5 * wall_.GetRegionLength(region.circle().diameter()),
                                    app_->rand());
      pos.x += x_offset;
      pos.y += y_offset;
      return glm::vec3(pos, z);
    }

    const RectangleTargetRegion& rect = region.rectangle();
    glm::vec2 pos = GetRandomPositionInRectangle(wall_.GetRegionLength(rect.x_length()),
                                                 wall_.GetRegionLength(rect.y_length()),
                                                 wall_.GetRegionLength(rect.inner_x_length()),
                                                 wall_.GetRegionLength(rect.inner_y_length()),
                                                 app_->rand());
    pos.x += x_offset;
    pos.y += y_offset;
    return glm::vec3(pos, z);
  }

  glm::vec2 GetFixedDistanceAdjustedPoint(const glm::vec2& point, const TargetRegion& region) {
    Target* most_recent_target = target_manager_->GetMutableMostRecentlyAddedTarget();
    if (most_recent_target == nullptr) {
      return point;
    }
    float distance = app_->rand().GetJittered(
        wall_.GetRegionLength(region.fixed_distance_from_last_target()),
        wall_.GetRegionLength(region.fixed_distance_from_last_target_jitter()));
    // This can't just drop the y component and take x,z from world position because for cylinder
    // walls we wrap the flat wall around the circle.
    glm::vec2 dir = glm::normalize(point - *most_recent_target->wall_position);

    return *most_recent_target->wall_position + (dir * distance);
  }

  Wall wall_;
  TargetPlacementStrategy strategy_;
  TargetManager* target_manager_;
  Application* app_;
  ProfileSelectionContext selection_context_;
  bool logged_cant_place_ = false;
};

}  // namespace

std::unique_ptr<WallTargetPlacer> CreateWallTargetPlacer(const Wall& wall,
                                                         const TargetPlacementStrategy& strategy,
                                                         TargetManager* target_manager,
                                                         Application* app) {
  return std::make_unique<WallTargetPlacerImpl>(wall, strategy, target_manager, app);
}

}  // namespace aim
