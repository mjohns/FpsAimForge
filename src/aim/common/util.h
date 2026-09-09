#pragma once

#include <string>

#include "aim/common/simple_types.h"
#include "aim/proto/common.pb.h"
#include "glm/vec2.hpp"  // IWYU pragma: keep
#include "glm/vec3.hpp"  // IWYU pragma: keep
#include "imgui.h"

namespace aim {

glm::vec3 ToVec3(const StoredVec3& v);
glm::vec3 ToVec3(const StoredColor& c);
glm::vec3 ToVec3(const StoredRgb& c);
glm::vec2 ToVec2(const StoredVec2& v);
StoredVec3 ToStoredVec3(float x, float y, float z);

template <typename T>
static ImVec2 ToImVec2(const T& v) {
  return ImVec2(v.x, v.y);
}

template <typename T>
static glm::vec2 ToVec2(const T& v) {
  return glm::vec2(v.x, v.y);
}

template <typename T>
static StoredVec3 ToStoredVec3(const T& v) {
  StoredVec3 result;
  result.set_x(v.x);
  result.set_y(v.y);
  result.set_z(v.z);
  return result;
}

i32 FloatColorTo255(float value);
Rgb HexToRgb(std::string hex);
ImColor HexToImColor(std::string hex);
std::string ToHexString(const StoredColor& c);
std::string ToHexString(const StoredRgb& c);
StoredRgb ToStoredRgb(const StoredColor& c);
StoredRgb ToStoredRgb(i32 r, i32 g, i32 b);
StoredColor ToStoredColor(const std::string& hex);
StoredColor ToStoredColor(i32 r, i32 g, i32 b);
StoredColor FloatToStoredColor(float r, float g, float b);
StoredRgb FloatToStoredRgb(float r, float g, float b);
StoredColor ToStoredColor(float gray_value);
ImU32 ToImCol32(const StoredRgb& c, uint8_t alpha = 255);
ImU32 ToImCol32(const StoredColor& c);

std::string MaybeIntToString(float value, int decimal_places = 1);

static std::string FirstNonEmpty(const std::string& v1, const std::string& v2) {
  return v1.size() > 0 ? v1 : v2;
}

static bool IsZero(const StoredVec3& v) {
  return v.x() == 0 && v.y() == 0 && v.z() == 0;
}

float FirstNonZero(float v1, float v2);

static float FirstGreaterThanZero(float val1, float val2) {
  return val1 > 0 ? val1 : val2;
}

static float ClampPositive(float val) {
  return val > 0 ? val : 0.0f;
}

static void EnsureNegative(float* val) {
  if (*val > 0) {
    *val *= -1;
  }
}

static void EnsurePositive(float* val) {
  if (*val < 0) {
    *val *= -1;
  }
}

static float GetStopDistance(float speed, float acceleration) {
  return (speed * speed) / (2 * acceleration);
}

static float GetStopTime(float speed, float acceleration) {
  return speed / acceleration;
}

static float GetSpeedToStopInTime(float time, float acceleration) {
  return time * acceleration;
}

static float GetStartSpeedForStopDistance(float stop_distance, float acceleration) {
  float speed_squared = 2.0f * acceleration * stop_distance;
  return std::sqrt(speed_squared);
}

}  // namespace aim
