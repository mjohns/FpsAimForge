#pragma once

#include <optional>

#include "aim/common/field.h"
#include "aim/proto/scenario.pb.h"

namespace aim {

enum StrafeDirectionType : int {
  LEFT_RIGHT = 0,
  UP_DOWN = 1,
  FORWARD_BACK = 2,
};

struct StrafeDirectionInfo {
  StrafeDirectionType strafe_direction;
  PtrField<ProfileListInfo> profiles_info;
  google::protobuf::RepeatedPtrField<StrafeProfile>* profile_list;
  Bounds* bounds;
  Bounds* relative_bounds;
  Field<Direction> direction_field;

  void SetStrafes(const StrafeDirectionInfo& other) {
    if (other.profiles_info.has()) {
      *profiles_info.get_mutable() = other.profiles_info.get();
    } else {
      profiles_info.clear();
    }

    *profile_list = *other.profile_list;
    if (bounds != nullptr) {
      *bounds = other.bounds != nullptr ? *other.bounds : Bounds();
    }
    if (relative_bounds != nullptr) {
      *relative_bounds = other.relative_bounds != nullptr ? *other.relative_bounds : Bounds();
    }
    if (other.direction_field.has()) {
      direction_field.set(other.direction_field.get());
    } else {
      direction_field.clear();
    }
  }
};

struct StrafeInfo {
  std::optional<StrafeDirectionInfo> left_right;
  std::optional<StrafeDirectionInfo> up_down;
  std::optional<StrafeDirectionInfo> forward_back;
};

StrafeInfo GetStrafeInfo(ScenarioDef& def);
void DrawStrafeTab(StrafeDirectionInfo& strafe_info);
void DrawStrafeTabs(ScenarioDef& def);

}  // namespace aim
