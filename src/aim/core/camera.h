#pragma once

#include <optional>

#include "aim/common/simple_types.h"
#include "aim/proto/scenario.pb.h"
#include "glm/mat4x4.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"    // IWYU pragma: keep

namespace aim {

// Convert cm/360 to radians per dot reported by mouse at the given dpi.
float CmPer360ToRadiansPerDot(float cm_per_360, float dpi);

float GetMaxPitch();

struct LookAtInfo {
  glm::vec3 front;
  glm::vec3 right;
  glm::vec3 up;
  glm::vec3 position;
  glm::mat4 transform;
};

glm::mat4 GetPerspectiveTransformation(const ScreenInfo& screen, float horizontal_fov_degrees = 0);

struct CameraParams {
  CameraParams() {}
  CameraParams(const Room& room);

  glm::vec3 position{0, 0, 0};
  glm::vec3 up{0, 0, 1};
  glm::vec3 front{0, 1, 0};
  float pitch = 0;
  float yaw = 0;

  std::optional<glm::vec3> look_at_point;
};

class Camera {
 public:
  Camera();
  explicit Camera(const CameraParams& params);

  LookAtInfo GetLookAt();

  const glm::vec3& GetPosition() const {
    return position_;
  }

  float GetPitch() {
    return pitch_;
  }

  float GetYaw() {
    return yaw_;
  }

  void SetPitchYawLookingAtPoint(const glm::vec3& look_at_pos);

  void Update(int xrel, int yrel, float radians_per_dot);
  void UpdatePitch(float pitch);
  void UpdateYaw(float yaw);
  void UpdatePitchYaw(const PitchYaw& pitch_yaw);
  void UpdatePosition(const glm::vec3& position);

 private:
  glm::vec3 position_;
  glm::vec3 up_;
  glm::vec3 front_;
  glm::vec3 right_;
  float pitch_ = 0;
  float yaw_ = 0;
  bool is_default_orientation_;
};

}  // namespace aim
