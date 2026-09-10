#include "wall.h"

#include <optional>

#include "aim/common/geometry.h"
#include "glm/gtc/constants.hpp"
#include "glm/vec2.hpp"  // IWYU pragma: keep

namespace aim {

bool Wall::IsPointInBounds(const glm::vec2& point, float padding) const {
  if (is_barrel) {
    return IsPointInCircle(point, width - padding);
  }
  float half_width = (width - padding) / 2.0f;
  float half_height = (height - padding) / 2.0f;
  glm::vec2 bottom_left(-1 * half_width, -1 * half_height);
  glm::vec2 top_right(half_width, half_height);
  return IsPointInRectangle(point, bottom_left, top_right);
}

float Wall::GetRegionLength(const RegionLength& length) const {
  if (length.has_value()) {
    return length.value();
  }
  if (length.has_x_percent_value()) {
    return width * length.x_percent_value();
  }
  if (length.has_y_percent_value()) {
    return height * length.y_percent_value();
  }
  if (length.has_depth_percent_value()) {
    return depth * length.depth_percent_value();
  }
  return 0;
}

glm::vec2 Wall::GetRegionVec2(const RegionVec2& v) const {
  return glm::vec2(GetRegionLength(v.x()), GetRegionLength(v.y()));
}

WallBounds Wall::GetWallBounds(const Bounds& b) const {
  WallBounds result;
  float width_value = b.has_width() ? GetRegionLength(b.width()) : 0.9 * this->width;
  result.min_x = -0.5 * width_value;
  result.max_x = 0.5 * width_value;

  float height_value = b.has_height() ? GetRegionLength(b.height()) : 0.9 * this->height;
  result.min_y = (-0.5 * height_value);
  result.max_y = (0.5 * height_value);

  float max_depth = b.has_depth() ? GetRegionLength(b.depth()) : 0.5 * this->depth;
  result.min_depth = 0;
  result.max_depth = max_depth;

  return result;
}

WallRelativeBounds Wall::GetWallRelativeBounds(const Bounds& bounds) const {
  WallRelativeBounds relative_bounds;
  const WallBounds b = GetWallBounds(bounds);
  if (bounds.has_height()) {
    relative_bounds.min_y = b.min_y;
    relative_bounds.max_y = b.max_y;
  }
  if (bounds.has_width()) {
    relative_bounds.min_x = b.min_x;
    relative_bounds.max_x = b.max_x;
  }
  if (bounds.has_depth()) {
    relative_bounds.min_depth = b.min_depth;
    relative_bounds.max_depth = b.max_depth;
  }
  return relative_bounds;
}

Wall Wall::ForRoom(const Room& room) {
  Wall wall{};
  if (room.has_cylinder_room()) {
    if (room.cylinder_room().width_degrees() > 0) {
      wall.width = (room.cylinder_room().width_degrees() / 360.0f) * glm::two_pi<float>() *
                   room.cylinder_room().radius();
    } else if (room.cylinder_room().width_perimeter_percent() > 0) {
      wall.width = room.cylinder_room().width_perimeter_percent() * glm::two_pi<float>() *
                   room.cylinder_room().radius();
    } else {
      wall.width = room.cylinder_room().width();
    }
    wall.height = room.cylinder_room().height();
    wall.depth = room.cylinder_room().radius() - room.camera_position().y();
  } else if (room.has_barrel_room()) {
    wall.width = room.barrel_room().radius() * 2;
    wall.height = wall.width;
    wall.depth = abs(room.camera_position().y());
    wall.is_barrel = true;
  } else if (room.has_simple_room()) {
    wall.width = room.simple_room().width();
    wall.height = room.simple_room().height();
    wall.depth = room.simple_room().depth();
    if (wall.depth <= 0) {
      wall.depth = abs(room.camera_position().y());
    }
  }
  return wall;
}

}  // namespace aim
