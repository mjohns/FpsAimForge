#include "strafe_editor.h"

#include "aim/common/field.h"
#include "aim/common/imgui_ext.h"
#include "aim/proto/scenario.pb.h"
#include "aim/ui/editor/profile_list_editor.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

void DrawStrafeProfile(float char_x,
                       RegionLength::TypeCase default_region_length_type,
                       StrafeProfile* p) {
  TimeOrDistance type = p->has_time() ? TimeOrDistance::TIME : TimeOrDistance::DISTANCE;
  ImGui::SimpleTypeDropdown("TimeOrDistanceDropdown", &type, kTimeOrDistance, char_x * 9);
  ImGui::SameLine();
  if (type == TimeOrDistance::TIME) {
    ImGui::InputJitteredFloat(
        ImGui::InputFloatParams("Time").set_step(0.1, 0.5).set_min(0.1).set_default(1).set_width(
            char_x * 10),
        PROTO_JITTERED_FIELD(StrafeProfile, p, time));
    p->clear_distance();
    p->clear_distance_jitter();
  } else {
    DrawJitteredRegionLengthEditor("Distance",
                                   default_region_length_type,
                                   p->mutable_distance(),
                                   p->mutable_distance_jitter(),
                                   30);
    p->clear_time();
    p->clear_time_jitter();
  }
  ImGui::InputJitteredFloat(
      GetDefaultMultiplierInputParams("Speed multiplier").set_is_optional().set_width(char_x * 10),
      PROTO_JITTERED_FIELD(StrafeProfile, p, speed_multiplier));
  ImGui::InputJitteredFloat(GetDefaultMultiplierInputParams("Acceleration multiplier")
                                .set_is_optional()
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(StrafeProfile, p, acceleration_multiplier));
  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Start speed percent")
                        .set_is_optional()
                        .set_step(1, 10)
                        .set_min(1)
                        .set_default(100)
                        .set_width(char_x * 10),
                    PROTO_PERCENT_FIELD(StrafeProfile, p, start_speed_percent));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "For a new target with acceleartion. What percent of max speed to start at. The default is "
      "to start at 0 speed.");
  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Center bias")
                        .set_is_optional()
                        .set_step(0.1, 0.5)
                        .set_min(0.1)
                        .set_default(0.1)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(StrafeProfile, p, center_bias));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "If close to the edge will shorten/lengthen the next strafe to encourage moving towards the "
      "center. 0.10 means lengthen the strafe by 10%");

  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Pause time")
                                .set_is_optional()
                                .set_step(0.1, 0.2)
                                .set_min(0)
                                .set_default(0.3)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(StrafeProfile, p, pause_time));
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Amount of time to pause at the end of the strafe in seconds"));

  if (p->has_pause_time()) {
    ImGui::Indent();
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Pause chance")
                          .set_is_optional()
                          .set_step(1, 10)
                          .set_min(1)
                          .set_default(50)
                          .set_width(char_x * 10),
                      PROTO_PERCENT_FIELD(StrafeProfile, p, pause_chance_percent));
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("Percent chance that the target will pause at the end of the strafe"));
    ImGui::Unindent();
  } else {
    p->clear_pause_chance_percent();
  }

  ImGui::InputJitteredFloat(GetDefaultMultiplierInputParams("Target radius multiplier")
                                .set_is_optional()
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(StrafeProfile, p, target_radius_multiplier));
}

void DrawLeftRightStrafeProfiles(PtrField<ProfileListInfo> profiles_info,
                                 google::protobuf::RepeatedPtrField<StrafeProfile>* profile_list,
                                 Bounds* bounds,
                                 Bounds* relative_bounds,
                                 Field<Direction> direction_field) {
  ImGui::IdGuard cid("LeftRightProfiles");
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Bounds"));
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("Width",
                                 RegionLength::kXPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, Bounds, bounds, width),
                                 90);
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Constrain where the target can strafe on the wall"));

  if (relative_bounds != nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Relative bounds"));
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("RelativeWidth",
                                   RegionLength::kXPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, relative_bounds, width),
                                   40);
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("Constrain movement based on the initial target position"));
  }

  float char_x = ImGui::GetDefaultCharSizeX();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Initial left/right direction"));
  ImGui::SameLine();
  Direction direction = direction_field.get();
  ImGui::SimpleTypeDropdown(
      "LeftRightDirectionTypeDropdown", &direction, kLeftRightDirections, char_x * 18);
  direction_field.set(direction);

  ImGui::SpacedSeparator();

  DrawProfileList("LeftRightProfileList",
                  "Profile",
                  profiles_info,
                  profile_list,
                  std::bind_front(&DrawStrafeProfile, char_x, RegionLength::kXPercentValue));
}

void DrawUpDownStrafeProfiles(PtrField<ProfileListInfo> profiles_info,
                              google::protobuf::RepeatedPtrField<StrafeProfile>* profile_list,
                              Bounds* bounds,
                              Bounds* relative_bounds,
                              Field<Direction> direction_field) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("UpDownProfiles");
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Bounds"));
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("Height",
                                 RegionLength::kYPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, Bounds, bounds, height),
                                 90);
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Constrain where the target can strafe on the wall"));

  if (relative_bounds != nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Relative bounds"));
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("RelativeHeight",
                                   RegionLength::kYPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, relative_bounds, height),
                                   40);
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("Constrain movement based on the initial target position"));
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Initial up/down direction"));
  ImGui::SameLine();
  Direction direction = direction_field.get();
  ImGui::SimpleTypeDropdown(
      "UpDownDirectionTypeDropdown", &direction, kUpDownDirections, char_x * 18);
  direction_field.set(direction);

  ImGui::SpacedSeparator();

  DrawProfileList("UpDownProfileList",
                  "Profile",
                  profiles_info,
                  profile_list,
                  std::bind_front(&DrawStrafeProfile, char_x, RegionLength::kYPercentValue));
}

void DrawForwardBackStrafeProfiles(PtrField<ProfileListInfo> profiles_info,
                                   google::protobuf::RepeatedPtrField<StrafeProfile>* profile_list,
                                   Bounds* bounds,
                                   Bounds* relative_bounds,
                                   Field<Direction> direction_field) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("ForwardBackProfiles");
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Bounds"));
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("Depth",
                                 RegionLength::kDepthPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, Bounds, bounds, depth),
                                 50);
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Constrain where the target can move forward and back"));

  if (relative_bounds != nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Relative bounds"));
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("RelativeDepth",
                                   RegionLength::kDepthPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, relative_bounds, depth),
                                   20);
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("Constrain movement based on the initial target position"));
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Initial forward/back direction"));
  ImGui::SameLine();
  Direction direction = direction_field.get();
  ImGui::SimpleTypeDropdown(
      "ForwardBackDirectionTypeDropdown", &direction, kForwardBackDirections, char_x * 18);
  direction_field.set(direction);

  ImGui::SpacedSeparator();

  DrawProfileList("ForwardBackProfileList",
                  "Profile",
                  profiles_info,
                  profile_list,
                  std::bind_front(&DrawStrafeProfile, char_x, RegionLength::kDepthPercentValue));
}

}  // namespace

StrafeInfo GetStrafeInfo(ScenarioDef& def) {
  StrafeInfo info;
  auto scenario_type = def.type_case();

  if (scenario_type == ScenarioDef::kStrafeDef) {
    auto* d = def.mutable_strafe_def();
    info.left_right = {
        StrafeDirectionType::LEFT_RIGHT,
        PROTO_PTR_FIELD(ProfileListInfo, StrafeScenarioDef, d, left_right_profiles_info),
        d->mutable_left_right_profiles(),
        d->mutable_bounds(),
        d->mutable_relative_bounds(),
        PROTO_FIELD(Direction, StrafeScenarioDef, d, left_right_initial_direction),
    };
    info.up_down = {
        StrafeDirectionType::UP_DOWN,
        PROTO_PTR_FIELD(ProfileListInfo, StrafeScenarioDef, d, up_down_profiles_info),
        d->mutable_up_down_profiles(),
        d->mutable_bounds(),
        d->mutable_relative_bounds(),
        PROTO_FIELD(Direction, StrafeScenarioDef, d, up_down_initial_direction),
    };
    info.forward_back = {
        StrafeDirectionType::FORWARD_BACK,
        PROTO_PTR_FIELD(ProfileListInfo, StrafeScenarioDef, d, forward_back_profiles_info),
        d->mutable_forward_back_profiles(),
        d->mutable_bounds(),
        d->mutable_relative_bounds(),
        PROTO_FIELD(Direction, StrafeScenarioDef, d, forward_back_initial_direction),
    };
  }

  if (scenario_type == ScenarioDef::kBounceDef) {
    auto* d = def.mutable_bounce_def();
    info.left_right = {
        StrafeDirectionType::LEFT_RIGHT,
        PROTO_PTR_FIELD(ProfileListInfo, BounceScenarioDef, d, left_right_profiles_info),
        d->mutable_left_right_profiles(),
        d->mutable_bounds(),
        d->mutable_relative_bounds(),
        PROTO_FIELD(Direction, BounceScenarioDef, d, left_right_initial_direction),
    };
    info.forward_back = {
        StrafeDirectionType::FORWARD_BACK,
        PROTO_PTR_FIELD(ProfileListInfo, BounceScenarioDef, d, forward_back_profiles_info),
        d->mutable_forward_back_profiles(),
        d->mutable_bounds(),
        d->mutable_relative_bounds(),
        PROTO_FIELD(Direction, BounceScenarioDef, d, forward_back_initial_direction),
    };
  }

  if (scenario_type == ScenarioDef::kAngleStrafeDef) {
    auto* d = def.mutable_angle_strafe_def();
    info.forward_back = {
        StrafeDirectionType::FORWARD_BACK,
        PROTO_PTR_FIELD(ProfileListInfo, AngleStrafeScenarioDef, d, forward_back_profiles_info),
        d->mutable_forward_back_profiles(),
        d->mutable_bounds(),
        /* relative_bounds= */ nullptr,
        PROTO_FIELD(Direction, AngleStrafeScenarioDef, d, forward_back_initial_direction),
    };
  }

  return info;
}

void DrawStrafeTab(StrafeDirectionInfo& info) {
  if (info.strafe_direction == StrafeDirectionType::LEFT_RIGHT) {
    if (BeginMaybeEditedTabItem("Left/Right", info.profile_list->size() > 0)) {
      ImGui::BeginChild("LeftRightContent");
      DrawLeftRightStrafeProfiles(info.profiles_info,
                                  info.profile_list,
                                  info.bounds,
                                  info.relative_bounds,
                                  info.direction_field);

      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }

  if (info.strafe_direction == StrafeDirectionType::UP_DOWN) {
    if (BeginMaybeEditedTabItem("Up/Down", info.profile_list->size() > 0)) {
      ImGui::BeginChild("UpDownContent");
      DrawUpDownStrafeProfiles(info.profiles_info,
                               info.profile_list,
                               info.bounds,
                               info.relative_bounds,
                               info.direction_field);

      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }

  if (info.strafe_direction == StrafeDirectionType::FORWARD_BACK) {
    if (BeginMaybeEditedTabItem("Forward/Back", info.profile_list->size() > 0)) {
      ImGui::BeginChild("ForwardBackContent");
      DrawForwardBackStrafeProfiles(info.profiles_info,
                                    info.profile_list,
                                    info.bounds,
                                    info.relative_bounds,
                                    info.direction_field);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
}

void DrawStrafeTabs(ScenarioDef& def) {
  auto info = GetStrafeInfo(def);
  if (info.left_right) {
    DrawStrafeTab(*info.left_right);
  }
  if (info.up_down) {
    DrawStrafeTab(*info.up_down);
  }
  if (info.forward_back) {
    DrawStrafeTab(*info.forward_back);
  }
}

}  // namespace aim
