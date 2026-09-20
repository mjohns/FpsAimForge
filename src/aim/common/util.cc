#include "util.h"

#include <stdlib.h>

#include <algorithm>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "aim/common/simple_types.h"
#include "aim/proto/common.pb.h"
#include "glm/ext/scalar_common.hpp"
#include "glm/trigonometric.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "google/protobuf/message.h"
#include "google/protobuf/util/message_differencer.h"
#include "imgui.h"

namespace aim {
namespace {

constexpr const int kMaxRgbValue = 255;

unsigned int HexStrToInt(const std::string& val) {
  unsigned int x;
  std::stringstream ss;
  ss << std::hex << val;
  ss >> x;
  return x;
}

}  // namespace

glm::vec3 ToVec3(const StoredVec3& v) {
  return glm::vec3(v.x(), v.y(), v.z());
}

glm::vec2 ToVec2(const StoredVec2& v) {
  return glm::vec2(v.x(), v.y());
}

StoredVec3 ToStoredVec3(float x, float y, float z) {
  StoredVec3 result;
  result.set_x(x);
  result.set_y(y);
  result.set_z(z);
  return result;
}

Rgb HexToRgb(std::string hex) {
  if (hex[0] == '#') {
    hex = hex.substr(1);
  }
  absl::AsciiStrToUpper(&hex);

  if (hex.length() != 6) {
    return Rgb();
  }
  Rgb v;
  v.r = HexStrToInt(hex.substr(0, 2));
  v.g = HexStrToInt(hex.substr(2, 2));
  v.b = HexStrToInt(hex.substr(4, 2));
  return v;
}

ImColor HexToImColor(std::string hex) {
  Rgb rgb = HexToRgb(hex);
  return ImColor(rgb.r, rgb.g, rgb.b);
}

std::string ToHexString(const StoredColor& c) {
  return ToHexString(ToStoredRgb(c));
}

std::string ToHexString(const StoredRgb& c) {
  int r = c.r();
  int g = c.g();
  int b = c.b();

  std::stringstream ss;
  ss << "#" << std::hex << std::setw(2) << std::setfill('0') << r << std::hex << std::setw(2)
     << std::setfill('0') << g << std::hex << std::setw(2) << std::setfill('0') << b;
  return ss.str();
}

StoredRgb ToStoredRgb(const StoredColor& c) {
  float mult = c.has_multiplier() ? c.multiplier() : 1.0f;
  int a = kMaxRgbValue;

  int r = c.r();
  int g = c.g();
  int b = c.b();
  if (c.has_hex()) {
    auto v = HexToRgb(c.hex());
    r = v.r;
    g = v.g;
    b = v.b;
  }
  StoredRgb result;
  result.set_r(glm::clamp((int)(r * mult), 0, kMaxRgbValue));
  result.set_g(glm::clamp((int)(g * mult), 0, kMaxRgbValue));
  result.set_b(glm::clamp((int)(b * mult), 0, kMaxRgbValue));
  return result;
}

StoredRgb ToStoredRgb(i32 r, i32 g, i32 b) {
  StoredRgb result;
  result.set_r(r);
  result.set_g(g);
  result.set_b(b);
  return result;
}

StoredColor ToStoredColor(const std::string& hex) {
  StoredColor c;
  c.set_hex(hex);
  return c;
}

StoredColor ToStoredColor(i32 r, i32 g, i32 b) {
  StoredColor result;
  result.set_r(r);
  result.set_g(g);
  result.set_b(b);
  return result;
}

StoredColor ToStoredColor(float gray_value) {
  StoredColor result;
  int val = kMaxRgbValue * gray_value;
  result.set_r(val);
  result.set_g(val);
  result.set_b(val);
  return result;
}

ImU32 ToImCol32(const StoredRgb& c, uint8_t alpha) {
  return IM_COL32(c.r(), c.g(), c.b(), alpha);
}

ImU32 ToImCol32(const StoredColor& c) {
  int a = kMaxRgbValue;
  StoredRgb v = ToStoredRgb(c);
  return IM_COL32(v.r(), v.g(), v.b(), a);
}

glm::vec3 ToVec3(const StoredColor& c) {
  StoredRgb v = ToStoredRgb(c);
  return ToVec3(v);
}

glm::vec3 ToVec3(const StoredRgb& v) {
  return glm::vec3(
      (float)v.r() / kMaxRgbValue, (float)v.g() / kMaxRgbValue, (float)v.b() / kMaxRgbValue);
}

std::string MaybeIntToString(float value, int decimal_places) {
  std::string s = std::to_string(value);

  size_t dot_pos = s.find('.');
  if (dot_pos == std::string::npos) {
    return s;
  }

  size_t decimals_start = dot_pos + 1;

  int last_non_zero = -1;
  for (size_t i = 0; i < decimal_places && (decimals_start + i) < s.size(); ++i) {
    bool keep = s[decimals_start + i] != '0';
    if (keep) {
      last_non_zero = i;
    }
  }

  if (last_non_zero < 0) {
    return s.substr(0, dot_pos);
  }
  return s.substr(0, decimals_start + last_non_zero + 1);
}

float FirstNonZero(float v1, float v2) {
  return v1 != 0 ? v1 : v2;
}

i32 FloatColorTo255(float value) {
  i32 result = 255 * value;
  if (result < 0) {
    return 0;
  }
  if (result > 255) {
    return 255;
  }
  return result;
}

StoredColor FloatToStoredColor(float r, float g, float b) {
  return ToStoredColor(FloatColorTo255(r), FloatColorTo255(g), FloatColorTo255(b));
}

StoredRgb FloatToStoredRgb(float r, float g, float b) {
  return ToStoredRgb(FloatToStoredColor(r, g, b));
}

}  // namespace aim
