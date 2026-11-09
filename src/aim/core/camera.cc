#include "camera.h"

#include "aim/common/geometry.h"
#include "aim/common/util.h"
#include "aim/common/wall.h"
#include "aim/core/target.h"
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/gtc/matrix_transform.hpp"  // IWYU pragma: keep
#include "glm/mat4x4.hpp"                // IWYU pragma: keep
#include "glm/trigonometric.hpp"
#include "glm/vec3.hpp"  // IWYU pragma: keep

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/rotate_vector.hpp"
#include "glm/gtx/vector_angle.hpp"

namespace aim {
namespace {

constexpr float kMaxPitch = glm::radians(89.0f);
constexpr float kMinPitch = -1 * kMaxPitch;

glm::vec3 GetNormalizedRight(const glm::vec3& v, const glm::vec3& up) {
  return glm::normalize(glm::cross(v, up));
}

PitchYaw GetPitchYawFromLookAtDefaultOrientation(const glm::vec3& look_at) {
  auto v = glm::normalize(look_at);
  PitchYaw result;
  result.pitch = asin(v.z);
  result.yaw = atan2(v.x, v.y);
  return result;
}

PitchYaw GetPitchYawFromLookAt(const glm::vec3& look_at,
                               const glm::vec3& up,
                               const glm::vec3& front,
                               const glm::vec3& right) {
  glm::vec3 transformed_look_at =
      MakeCoordinateSystemTransform(right, front, up) * glm::vec4(look_at, 1.0f);
  return GetPitchYawFromLookAtDefaultOrientation(transformed_look_at);
}

void FillInLookAt(const glm::vec3& position, const glm::vec3& camera_up, LookAtInfo* info) {
  info->position = position;
  info->front = glm::normalize(info->front);
  info->right = GetNormalizedRight(info->front, camera_up);
  info->up = glm::normalize(glm::cross(info->right, info->front));
  info->transform = glm::lookAt(position, position + info->front, info->up);
}

float ClampPitch(float pitch) {
  return glm::clamp(pitch, kMinPitch, kMaxPitch);
}

}  // namespace

float CmPer360ToRadiansPerDot(float cm_per_360, float dpi) {
  float dots_per_cm = dpi / 2.54f;  // 1 inch = 2.54 cm
  float dots_per_360 = cm_per_360 * dots_per_cm;
  // Convert to radians.
  return glm::two_pi<float>() / dots_per_360;
}

void Camera::SetPitchYawLookingAtPoint(const glm::vec3& look_at_pos) {
  glm::vec3 initial_look_at = glm::normalize(look_at_pos - GetPosition());
  auto pitch_yaw = GetPitchYawFromLookAt(initial_look_at, up_, front_, right_);
  UpdatePitchYaw(pitch_yaw);
}

glm::mat4 GetPerspectiveTransformation(const ScreenInfo& screen, float horizontal_fov_degrees) {
  if (horizontal_fov_degrees <= 0) {
    horizontal_fov_degrees = 103.0f;
  }
  float aspect_ratio = (float)screen.width / (float)screen.height;
  float horizontal_fov = glm::radians(horizontal_fov_degrees);
  float tan_half_horizontal_fov = glm::tan(horizontal_fov / 2.0f);
  float tan_half_vertical_fov = tan_half_horizontal_fov / aspect_ratio;
  float vertical_fov = 2.0f * glm::atan(tan_half_vertical_fov);
  return glm::perspective(vertical_fov, aspect_ratio, 0.1f, 2000.0f);
}

LookAtInfo Camera::GetLookAt() {
  LookAtInfo info;
  if (is_default_orientation_) {
    // sin(0) = 0, cos(0) = 1, sin(90) = 1, cos(90) = 0
    info.front.x = cos(pitch_) * sin(yaw_);
    info.front.y = cos(pitch_) * cos(yaw_);
    info.front.z = sin(pitch_);
  } else {
    info.front = front_;
    info.front = glm::rotate(info.front, pitch_, right_);
    info.front = glm::rotate(info.front, -1 * yaw_, up_);
  }

  FillInLookAt(position_, up_, &info);
  return info;
}

void Camera::Update(int xrel, int yrel, float radians_per_dot) {
  yaw_ += radians_per_dot * xrel;
  pitch_ -= radians_per_dot * yrel;
  pitch_ = ClampPitch(pitch_);
  // TODO: clamp yaw?
}

void Camera::UpdatePosition(const glm::vec3& position) {
  position_ = position;
}

void Camera::UpdatePitch(float pitch) {
  pitch_ = ClampPitch(pitch);
}
void Camera::UpdateYaw(float yaw) {
  yaw_ = yaw;
}

void Camera::UpdatePitchYaw(const PitchYaw& pitch_yaw) {
  UpdatePitch(pitch_yaw.pitch);
  UpdateYaw(pitch_yaw.yaw);
}

Camera::Camera() : Camera(CameraParams{}) {}
Camera::Camera(const CameraParams& params)
    : position_(params.position),
      up_(glm::normalize(params.up)),
      front_(glm::normalize(params.front)),
      right_(glm::normalize(glm::cross(params.front, params.up))),
      pitch_(ClampPitch(params.pitch)),
      yaw_(params.yaw) {
  is_default_orientation_ = front_.y == 1 && up_.z == 1;
  if (params.look_at_point.has_value()) {
    SetPitchYawLookingAtPoint(*params.look_at_point);
  }
}

CameraParams::CameraParams(const Room& room) {
  pitch = room.start_pitch();
  yaw = room.start_yaw();
  position = ToVec3(room.camera_position());
  if (room.has_camera_up()) {
    up = ToVec3(room.camera_up());
  }
  if (room.has_camera_front()) {
    front = ToVec3(room.camera_front());
  }
  if (room.has_look_at_point()) {
    Wall wall = Wall::ForRoom(room);
    glm::vec2 wall_pos = wall.GetRegionVec2(room.look_at_point());
    look_at_point = WallPositionToWorldPosition(wall_pos, /*target_radius=*/1.3, room, /*depth=*/0);
  }
}

float GetMaxPitch() {
  return kMaxPitch;
}

}  // namespace aim
