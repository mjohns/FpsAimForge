#include "scenario_type_editor.h"

#include <format>
#include <functional>
#include <optional>

#include "aim/common/field.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/core/application.h"
#include "aim/core/scenario_manager.h"
#include "aim/scenario/scenario_overrides.h"
#include "aim/ui/editor/profile_list_editor.h"
#include "aim/ui/editor/room_editor.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "aim/ui/editor/scenario_editor_screen.h"
#include "aim/ui/search_selector.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace aim {
namespace {

void DrawWallArcEditor(WallArcScenarioDef& d) {
  ImGui::IdGuard cid("WallArcEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Width");
  ImGui::SameLine();
  DrawRegionLengthEditor("Width", RegionLength::kXPercentValue, d.mutable_width(), 50);
  ImGui::SameLine();
  ImGui::HelpMarker("The arc will be stretched over the specified width");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Height");
  ImGui::SameLine();
  DrawRegionLengthEditor("Height", RegionLength::kYPercentValue, d.mutable_height(), 50);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Height range");
  ImGui::SameLine();
  bool use_range = d.has_height_jitter();
  ImGui::Checkbox("##UseRange", &use_range);
  if (use_range) {
    ImGui::SameLine();
    DrawRegionLengthEditor("Height range", RegionLength::kYPercentValue, d.mutable_height_jitter());
  } else {
    d.clear_height_jitter();
  }

  ImGui::InputBool("Reflect", PROTO_BOOL_FIELD(WallArcScenarioDef, &d, reflect));
  ImGui::SameLine();
  ImGui::HelpMarker("Turn the arc upside down.");

  ImGui::InputBool("Start on floor", PROTO_BOOL_FIELD(WallArcScenarioDef, &d, start_on_ground));
}

void DrawSineEditor(SineScenarioDef& d) {
  ImGui::IdGuard cid("SineEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Height");
  ImGui::SameLine();
  DrawRegionLengthEditor("Height", RegionLength::kYPercentValue, d.mutable_height(), 20);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Width");
  ImGui::SameLine();
  DrawRegionLengthEditor("Width", RegionLength::kXPercentValue, d.mutable_width(), 20);

  ImGui::InputBool("Going left", PROTO_BOOL_FIELD(SineScenarioDef, &d, going_left));
}

void DrawCircleEditor(CircleScenarioDef& d) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("CircleEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Radius");
  ImGui::SameLine();
  DrawRegionLengthEditor("Radius", RegionLength::kXPercentValue, d.mutable_radius(), 30);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Final radius");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("FinalRadius",
                                 RegionLength::kXPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, CircleScenarioDef, &d, final_radius),
                                 10);
  ImGui::SameLine();
  ImGui::HelpMarker(
      "The radius will change to this value over the duration of the scenario (or until "
      "direction change)");

  ImGui::InputFloat(ImGui::InputFloatParams("StartDegrees")
                        .set_label("Start degrees")
                        .set_step(5, 30)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(CircleScenarioDef, &d, start_degrees));
  ImGui::SameLine();
  ImGui::HelpMarker("0 degrees starts at 3 o'clock and rotates counter clockwise.");

  ImGui::InputBool("Start clockwise", PROTO_BOOL_FIELD(CircleScenarioDef, &d, rotate_clockwise));

  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Switch direction after time")
                        .set_is_optional()
                        .set_step(1, 5)
                        .set_min(5)
                        .set_default(30)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(CircleScenarioDef, &d, switch_after_seconds));

  ImGui::SpacedSeparator();

  ImGui::InputFloat(ImGui::InputFloatParams("StretchY")
                        .set_label("Stretch Y")
                        .set_step(0.05, 0.1)
                        .set_default(0.8)
                        .set_min(0.1)
                        .set_is_optional()
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(CircleScenarioDef, &d, stretch_y));
  ImGui::InputFloat(ImGui::InputFloatParams("StretchX")
                        .set_label("Stretch X")
                        .set_step(0.05, 0.1)
                        .set_default(0.8)
                        .set_min(0.1)
                        .set_is_optional()
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(CircleScenarioDef, &d, stretch_x));

  ImGui::SpacedSeparator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Depth");
  ImGui::SameLine();
  DrawRegionLengthEditor("Depth", RegionLength::kDepthPercentValue, d.mutable_depth(), 0);
  ImGui::SameLine();
  ImGui::HelpMarker("Distance away from the wall");
}

void DrawWallWanderProfile(float char_x, WallWanderProfile* p) {
  ImGui::InputJitteredFloat(ImGui::InputFloatParams("TimeBetweenTurns")
                                .set_label("Time between turns")
                                .set_step(0.1, 2)
                                .set_min(0.1)
                                .set_default(2)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(WallWanderProfile, p, turn_time));
  ImGui::SameLine();
  ImGui::HelpMarker("The amount of time to turn in a single direction before switching.");

  ImGui::InputJitteredFloat(ImGui::InputFloatParams("TurnRate")
                                .set_label("Turn rate")
                                .set_step(10, 30)
                                .set_default(300)
                                .set_min(0)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(WallWanderProfile, p, turn_rate));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "The number of degrees to turn per second. The turn rate will accelerate smoothly "
      "between "
      "turns base on turn time.");
}

void DrawWallWanderEditor(WallWanderScenarioDef& d) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("WallWanderEditor");

  if (d.profiles_size() == 0) {
    d.add_profiles();
  }
  ImGui::Text("Wander profiles");
  ImGui::Indent();
  DrawProfileList("WanderProfileList",
                  "Profile",
                  PROTO_PTR_FIELD(ProfileListInfo, WallWanderScenarioDef, &d, profiles_info),
                  d.mutable_profiles(),
                  std::bind_front(&DrawWallWanderProfile, char_x));
  ImGui::Unindent();

  ImGui::SpacedSeparator();
}

void DrawLinearEditor(LinearScenarioDef& d) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("LinearEditor");

  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Angle")
                                .set_step(1, 3)
                                .set_min(0)
                                .set_max(90)
                                .set_default(0)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(LinearScenarioDef, &d, angle));

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial left/right direction");
  ImGui::SameLine();
  Direction left_right_direction_type = d.has_left_right_initial_direction()
                                            ? d.left_right_initial_direction()
                                            : Direction::DIRECTION_IN;
  ImGui::SimpleTypeDropdown("LeftRightDirectionTypeDropdown",
                            &left_right_direction_type,
                            kLeftRightDirections,
                            char_x * 20);
  d.set_left_right_initial_direction(left_right_direction_type);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial up/down direction");
  ImGui::SameLine();
  Direction up_down_direction_type = d.up_down_initial_direction();
  ImGui::SimpleTypeDropdown(
      "UpDownDirectionTypeDropdown", &up_down_direction_type, kUpDownDirections, char_x * 20);
  d.set_up_down_initial_direction(up_down_direction_type);

  ImGui::SpacedSeparator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial target location");
  ImGui::Indent();
  DrawTargetPlacementStrategyEditor("Placement", d.mutable_target_placement_strategy());
  ImGui::Unindent();
}

void DrawBarrelEditor(ScenarioDef& def) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("BarrelEditor");
  BarrelScenarioDef& d = *def.mutable_barrel_def();

  if (!def.room().has_barrel_room()) {
    ImGui::Text("Must use barrel room");
    return;
  }

  ImGui::InputFloat(ImGui::InputFloatParams("DirectionRadiusPercent")
                        .set_label("Redirect to percent of center")
                        .set_step(1, 5)
                        .set_min(1)
                        .set_default(40)
                        .set_width(char_x * 10),
                    PROTO_PERCENT_FIELD(BarrelScenarioDef, &d, direction_radius_percent));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "When the target collides with the wall it will be redirected in the direction of a "
      "random "
      "point within the specified portion of the center. The smaller the radius the more it "
      "will "
      "be redirected towards the center of the circle.");

  if (!d.has_target_placement_strategy()) {
    d.mutable_target_placement_strategy()->mutable_min_distance()->set_value(15);
    CircleTargetRegion* region =
        d.mutable_target_placement_strategy()->add_regions()->mutable_circle();
    region->mutable_diameter()->set_x_percent_value(0.92);
    region->mutable_inner_diameter()->set_x_percent_value(0.6);
  }

  ImGui::SpacedSeparator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial target location");
  ImGui::Indent();
  DrawTargetPlacementStrategyEditor("Placement", d.mutable_target_placement_strategy());
  ImGui::Unindent();
}

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
  ImGui::HelpMarker("Amount of time to pause at the end of the strafe in seconds");

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
    ImGui::HelpMarker("Percent chance that the target will pause at the end of the strafe");
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
  ImGui::Text("Bounds");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("Width",
                                 RegionLength::kXPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, Bounds, bounds, width),
                                 90);
  ImGui::SameLine();
  ImGui::HelpMarker("Constrain where the target can strafe on the wall");

  if (relative_bounds != nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Relative bounds");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("RelativeWidth",
                                   RegionLength::kXPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, relative_bounds, width),
                                   40);
    ImGui::SameLine();
    ImGui::HelpMarker("Constrain movement based on the initial target position");
  }

  float char_x = ImGui::GetDefaultCharSizeX();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial left/right direction");
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
  ImGui::Text("Bounds");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("Height",
                                 RegionLength::kYPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, Bounds, bounds, height),
                                 90);
  ImGui::SameLine();
  ImGui::HelpMarker("Constrain where the target can strafe on the wall");

  if (relative_bounds != nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Relative bounds");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("RelativeHeight",
                                   RegionLength::kYPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, relative_bounds, height),
                                   40);
    ImGui::SameLine();
    ImGui::HelpMarker("Constrain movement based on the initial target position");
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial up/down direction");
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
  ImGui::Text("Bounds");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("Depth",
                                 RegionLength::kDepthPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, Bounds, bounds, depth),
                                 50);
  ImGui::SameLine();
  ImGui::HelpMarker("Constrain where the target can move forward and back");

  if (relative_bounds != nullptr) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Relative bounds");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("RelativeDepth",
                                   RegionLength::kDepthPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, relative_bounds, depth),
                                   20);
    ImGui::SameLine();
    ImGui::HelpMarker("Constrain movement based on the initial target position");
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Initial forward/back direction");
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

void DrawStrafeEditor(StrafeScenarioDef& d) {
  ImGui::IdGuard cid("StrafeEditor");
  float char_x = ImGui::GetDefaultCharSizeX();

  ImGui::Text("Left/right profiles");
  ImGui::Indent();
  DrawLeftRightStrafeProfiles(
      PROTO_PTR_FIELD(ProfileListInfo, StrafeScenarioDef, &d, left_right_profiles_info),
      d.mutable_left_right_profiles(),
      d.mutable_bounds(),
      d.mutable_relative_bounds(),
      PROTO_FIELD(Direction, StrafeScenarioDef, &d, left_right_initial_direction));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  ImGui::Text("Up/down profiles");
  ImGui::Indent();
  DrawUpDownStrafeProfiles(
      PROTO_PTR_FIELD(ProfileListInfo, StrafeScenarioDef, &d, up_down_profiles_info),
      d.mutable_up_down_profiles(),
      d.mutable_bounds(),
      d.mutable_relative_bounds(),
      PROTO_FIELD(Direction, StrafeScenarioDef, &d, up_down_initial_direction));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  ImGui::Text("Forward/back profiles");
  ImGui::Indent();
  DrawForwardBackStrafeProfiles(
      PROTO_PTR_FIELD(ProfileListInfo, StrafeScenarioDef, &d, forward_back_profiles_info),
      d.mutable_forward_back_profiles(),
      d.mutable_bounds(),
      d.mutable_relative_bounds(),
      PROTO_FIELD(Direction, StrafeScenarioDef, &d, forward_back_initial_direction));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  if (IsDefaultInstance(d.relative_bounds())) {
    d.clear_relative_bounds();
  }
  if (IsDefaultInstance(d.bounds())) {
    d.clear_bounds();
  }
}

void DrawBounceProfile(float char_x, BounceProfile* p) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Bounce height");
  ImGui::SameLine();
  DrawJitteredRegionLengthEditor("BounceHeight",
                                 RegionLength::kYPercentValue,
                                 p->mutable_height(),
                                 p->mutable_height_jitter(),
                                 30);

  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Bounce delay")
                                .set_step(0.01, 0.05)
                                .set_zero_is_unset()
                                .set_min(0)
                                .set_default(0)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(BounceProfile, p, delay_seconds));
  ImGui::InputBool("Only delay on floor", PROTO_BOOL_FIELD(BounceProfile, p, only_delay_on_floor));

  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Float time")
                                .set_step(0.01, 0.05)
                                .set_zero_is_unset()
                                .set_min(0)
                                .set_default(0)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(BounceProfile, p, float_time));

  ImGui::InputJitteredFloat(
      GetDefaultMultiplierInputParams("Speed multiplier").set_is_optional().set_width(char_x * 10),
      PROTO_JITTERED_FIELD(BounceProfile, p, speed_multiplier));

  ImGui::InputJitteredFloat(GetDefaultMultiplierInputParams("Acceleration multiplier")
                                .set_is_optional()
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(BounceProfile, p, acceleration_multiplier));

  ImGui::InputJitteredFloat(GetDefaultMultiplierInputParams("Downward speed multiplier")
                                .set_is_optional()
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(BounceProfile, p, down_speed_multiplier));

  ImGui::InputJitteredFloat(GetDefaultMultiplierInputParams("Downward acceleration multiplier")
                                .set_is_optional()
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(BounceProfile, p, down_acceleration_multiplier));
}

void DrawBounceEditor(BounceScenarioDef& d) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("BounceEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Floor height");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("FloorHeight",
                                 RegionLength::kYPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, BounceScenarioDef, &d, floor_height),
                                 0);
  ImGui::InputBool("Start on floor", PROTO_BOOL_FIELD(BounceScenarioDef, &d, start_on_floor));

  ImGui::SpacedSeparator();

  if (d.bounce_profiles_size() == 0) {
    d.add_bounce_profiles();
  }
  ImGui::Text("Bounce profiles");
  ImGui::Indent();
  DrawProfileList("BounceProfileList",
                  "Profile",
                  PROTO_PTR_FIELD(ProfileListInfo, BounceScenarioDef, &d, bounce_profiles_info),
                  d.mutable_bounce_profiles(),
                  std::bind_front(&DrawBounceProfile, char_x));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  ImGui::Text("Left/right profiles");
  ImGui::Indent();
  DrawLeftRightStrafeProfiles(
      PROTO_PTR_FIELD(ProfileListInfo, BounceScenarioDef, &d, left_right_profiles_info),
      d.mutable_left_right_profiles(),
      d.mutable_bounds(),
      d.mutable_relative_bounds(),
      PROTO_FIELD(Direction, BounceScenarioDef, &d, left_right_initial_direction));
  ImGui::Unindent();

  ImGui::SpacedSeparator();
  ImGui::Text("Forward/back profiles");
  ImGui::Indent();
  DrawForwardBackStrafeProfiles(
      PROTO_PTR_FIELD(ProfileListInfo, BounceScenarioDef, &d, forward_back_profiles_info),
      d.mutable_forward_back_profiles(),
      d.mutable_bounds(),
      d.mutable_relative_bounds(),
      PROTO_FIELD(Direction, BounceScenarioDef, &d, forward_back_initial_direction));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  if (IsDefaultInstance(d.bounds())) {
    d.clear_bounds();
  }
  if (IsDefaultInstance(d.relative_bounds())) {
    d.clear_relative_bounds();
  }
}

void DrawAngleStrafeProfile(float char_x, AngleStrafeProfile* p) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Distance");
  ImGui::SameLine();
  DrawJitteredRegionLengthEditor("Distance",
                                 RegionLength::kXPercentValue,
                                 p->mutable_distance(),
                                 p->mutable_distance_jitter(),
                                 30);

  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Angle")
                                .set_step(1, 3)
                                .set_min(0)
                                .set_max(60)
                                .set_default(0)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(AngleStrafeProfile, p, angle));

  if (p->angle() > 0 || p->angle_jitter() > 0) {
    ImGui::InputFloat(ImGui::InputFloatParams("DirectionChangePercent")
                          .set_label("Direction change chance")
                          .set_step(1, 5)
                          .set_range(0, 100)
                          .set_default(50)
                          .set_width(char_x * 12),
                      PROTO_PERCENT_FIELD(AngleStrafeProfile, p, direction_change_percent));
  } else {
    p->clear_direction_change_percent();
  }

  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Speed multiplier")
                                .set_is_optional()
                                .set_step(0.05, 0.2)
                                .set_min(0)
                                .set_default(1)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(AngleStrafeProfile, p, speed_multiplier));
  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Acceleration multiplier")
                                .set_is_optional()
                                .set_step(0.05, 0.2)
                                .set_min(0)
                                .set_default(1)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(AngleStrafeProfile, p, acceleration_multiplier));

  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Center bias")
                        .set_is_optional()
                        .set_step(0.1, 0.5)
                        .set_min(0.1)
                        .set_default(0.1)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(AngleStrafeProfile, p, center_bias));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "If close to the edge will shorten/lengthen the next strafe to encourage moving towards the "
      "center. 0.10 means lengthen the strafe by 10%");
}

void DrawAngleStrafeEditor(AngleStrafeScenarioDef& w) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("AngleStrafeEditor");
  DrawBoundsEditor("##Bounds", w.mutable_bounds());

  if (w.profiles_size() == 0) {
    w.add_profiles();
  }

  ImGui::SpacedSeparator();

  ImGui::Text("Strafe profiles");
  ImGui::Indent();
  DrawProfileList("StrafeProfileList",
                  "Profile",
                  PROTO_PTR_FIELD(ProfileListInfo, AngleStrafeScenarioDef, &w, profiles_info),
                  w.mutable_profiles(),
                  std::bind_front(&DrawAngleStrafeProfile, char_x));
  ImGui::Unindent();

  ImGui::Spacing();

  ImGui::SpacedSeparator();
}

void DrawCenteringEditor(CenteringScenarioDef& c) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("CenteringEditor");

  const char* kPoints = "Points";
  const char* kAngle = "Angle";

  std::string type = kPoints;
  if (c.has_angle()) {
    type = kAngle;
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Type");
  ImGui::SameLine();
  ImGui::SimpleDropdown("##TypeDrop", &type, {kPoints, kAngle}, char_x * 13);

  bool use_angle = type == kAngle;

  if (use_angle) {
    c.clear_wall_points();
  } else {
    c.clear_angle();
    c.clear_angle_length();
  }

  if (use_angle) {
    ImGui::Indent();
    ImGui::InputJitteredFloat(ImGui::InputFloatParams("Angle")
                                  .set_label("Angle degrees")
                                  .set_step(1, 5)
                                  .set_width(char_x * 12),
                              PROTO_JITTERED_FIELD(CenteringScenarioDef, &c, angle));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "Specify just the angle of movement and how far to travel. Typically used with Barrel "
        "rooms.");
    DrawRegionLengthEditor("Length", RegionLength::kXPercentValue, c.mutable_angle_length(), 50);
    ImGui::Unindent();
  } else {
    // Ensure two wall points.
    while (c.wall_points_size() < 2) {
      c.add_wall_points();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Point 1");
    ImGui::Indent();
    DrawRegionVec2Editor("Point1", c.mutable_wall_points(0));
    ImGui::Unindent();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Point 2");
    ImGui::Indent();
    DrawRegionVec2Editor("Point2", c.mutable_wall_points(1));
    ImGui::Unindent();

    int remove_at_i = -1;
    for (int i = 2; i < c.wall_points_size(); ++i) {
      ImGui::IdGuard lid("ExtraPoint", i);
      ImGui::AlignTextToFramePadding();
      ImGui::TextFmt("Point {}", i + 1);
      ImGui::SameLine();
      if (ImGui::Button(icons::kCancel)) {
        remove_at_i = i;
      }
      ImGui::Indent();
      DrawRegionVec2Editor("##PointEditor", c.mutable_wall_points(i));
      ImGui::Unindent();
    }

    ImGui::Spacing();
    if (ImGui::Button("Add point")) {
      c.add_wall_points();
    }

    if (remove_at_i > 0) {
      c.mutable_wall_points()->erase(c.mutable_wall_points()->begin() + remove_at_i);
    }
  }
}

void DrawStaticEditor(StaticScenarioDef& d) {
  ImGui::IdGuard cid("StaticEditor");
  DrawTargetPlacementStrategyEditor("Placement", d.mutable_target_placement_strategy());
}

void DrawWaypointEditor(WaypointScenarioDef& d) {
  ImGui::IdGuard cid("WaypointEditor");
  DrawTargetPlacementStrategyEditor("Placement", d.mutable_target_placement_strategy());
}

void DrawReferenceEditor(ScenarioDef& def,
                         Application* app,
                         std::string* error_message_out,
                         bool* editing_room,
                         ImGui::MultilineTextEntryDialog* description_dialog) {
  // Make sure only the appropriate fields are set on the def.
  ScenarioDef old_def = def;

  float char_x = ImGui::GetDefaultCharSizeX();

  def = {};
  if (old_def.has_level_overrides()) {
    *def.mutable_level_overrides() = old_def.level_overrides();
  }
  if (old_def.has_score_targets()) {
    *def.mutable_score_targets() = old_def.score_targets();
  }
  *def.mutable_overrides() = old_def.overrides();
  *def.mutable_reference_def() = old_def.reference_def();

  ReferenceScenarioDef& r = *def.mutable_reference_def();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Scenario");
  ImGui::SameLine();
  ImGui::HelpMarker("The name of the scenario to reference");
  ImGui::SameLine();

  if (app != nullptr) {
    ScenarioSearchInput(*app, r.mutable_scenario_name());
    bool has_referenced_scenario =
        app->scenario_manager().GetScenario(r.scenario_name()).has_value();
    if (has_referenced_scenario) {
      ImGui::SameLine();
      if (ImGui::Button(icons::kOpenInNew)) {
        // TODO: confirmation dialog and check if there are actual edits.
        app->scenario_manager().SetCurrentScenario(r.scenario_name());
        app->PopCurrentScreen();
        ScenarioEditorOptions opts;
        opts.scenario_name = r.scenario_name();
        app->PushNextScreen(CreateScenarioEditorScreen(opts));
      }
      ImGui::HelpTooltip("Go to the referenced scenario. All current edits will be lost.");
    }
  } else {
    ImGui::InputText("##ScenarioReference", r.mutable_scenario_name());
  }

  ImGui::SpacedSeparator();

  ImGui::Text("Overrides");
  ImGui::Indent();
  DrawOverridesEditor("ReferenceOverrides", def.mutable_overrides());
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  bool has_level_overrides = def.has_level_overrides();
  ImGui::Text("Level overrides");
  ImGui::SameLine();
  ImGui::Checkbox("##LevelOverridesCheck", &has_level_overrides);
  if (has_level_overrides) {
    ImGui::Indent();
    DrawOverridesEditor("LevelOverrides", def.mutable_level_overrides(), /*is_levels=*/true);
    ImGui::Unindent();
  } else {
    def.clear_level_overrides();
  }

  ImGui::SpacedSeparator();

  DrawScoreTargetsEditor(PROTO_PTR_FIELD(ScoreTargets, ScenarioDef, &def, score_targets));

  ImGui::SpacedSeparator();

  ImGui::InputInt(ImGui::InputIntParams::WithLabelAsId("Duration")
                      .set_is_optional()
                      .set_default(60)
                      .set_min(5)
                      .set_step(5, 5)
                      .set_width(char_x * 12),
                  PROTO_INT_FIELD(ReferenceScenarioDef, &r, duration_seconds));

  ImGui::InputInt(ImGui::InputIntParams::WithLabelAsId("Number of targets")
                      .set_is_optional()
                      .set_default(3)
                      .set_min(1)
                      .set_step(1, 5)
                      .set_width(char_x * 12),
                  PROTO_INT_FIELD(ReferenceScenarioDef, &r, num_targets));

  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Target radius")
                        .set_is_optional()
                        .set_default(2)
                        .set_min(0.01)
                        .set_step(0.05, 0.5)
                        .set_width(char_x * 12),
                    PROTO_FLOAT_FIELD(ReferenceScenarioDef, &r, explicit_target_radius));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "Provide an explicit radius for the target. Useful for providing a stable size for a base "
      "level.");

  if (app != nullptr) {
    bool has_room = r.has_room();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Change room");
    ImGui::SameLine();
    ImGui::Checkbox("##OverrideRoomCheck", &has_room);
    if (has_room) {
      if (!r.has_room()) {
        // Initialize room
        auto parent = app->scenario_manager().GetEvaluatedScenarioDef(r.scenario_name());
        if (parent) {
          *r.mutable_room() = parent->room();
        } else {
          *r.mutable_room() = GetDefaultSimpleRoom();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button(std::format("{} Room", icons::kEdit))) {
        *editing_room = true;
      }
    } else {
      r.clear_room();
    }
  }

  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Horizontal FOV")
                        .set_is_optional()
                        .set_step(1, 5)
                        .set_min(1)
                        .set_default(103)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(ReferenceScenarioDef, &r, horizontal_fov));

  ImGui::SpacedSeparator();

  bool has_shot_type = def.reference_def().has_shot_type();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Shot type");
  ImGui::SameLine();
  ImGui::Checkbox("##OverrideShotType", &has_shot_type);
  if (has_shot_type) {
    ImGui::Indent();
    DrawShotTypeEditor(*def.mutable_reference_def()->mutable_shot_type());
    ImGui::Unindent();
  } else {
    def.mutable_reference_def()->clear_shot_type();
  }

  if (description_dialog != nullptr) {
    ImGui::SpacedSeparator();
    std::string button_text = r.description().empty()
                                  ? std::format("{} Set description", icons::kEdit)
                                  : std::format("{} Edit description", icons::kEdit);
    if (ImGui::Button(button_text)) {
      description_dialog->NotifyOpen(r.description());
    }
    if (!r.description().empty()) {
      ImGui::SameLine();
      if (ImGui::SelectableButton(icons::kClear)) {
        r.clear_description();
      }
    }
  }

  ImGui::SpacedSeparator();

  if (app != nullptr) {
    if (ImGui::Button("Bake")) {
      auto parent = app->scenario_manager().GetEvaluatedScenarioDef(r.scenario_name());
      if (parent) {
        ScenarioDef baked_def = *parent;
        ApplyReferenceFieldOverrides(def, &baked_def);
        def = ApplyScenarioOverrides(baked_def);
      } else {
        *error_message_out =
            std::format("Referenced scenario \"{}\" is invalid.", r.scenario_name());
      }
    }
    ImGui::SameLine();
    ImGui::HelpMarker("Expand and remove the reference. Will now be an equivalent normal scenario");
  }
}

void InitializeScenarioType(ScenarioDef& def, ScenarioDef::TypeCase scenario_type) {
  auto target_placement = GetTargetPlacementStrategy(def);
  switch (scenario_type) {
    case ScenarioDef::kStaticDef:
      def.mutable_static_def();
      break;
    case ScenarioDef::kWaypointDef:
      def.mutable_waypoint_def();
    case ScenarioDef::kWallWanderDef:
      def.mutable_wall_wander_def();
      break;
    case ScenarioDef::kCenteringDef:
      def.mutable_centering_def();
      break;
    case ScenarioDef::kAngleStrafeDef:
      def.mutable_angle_strafe_def();
      break;
    case ScenarioDef::kStrafeDef:
      def.mutable_strafe_def();
      break;
    case ScenarioDef::kBounceDef:
      def.mutable_bounce_def();
      break;
    case ScenarioDef::kLinearDef:
      def.mutable_linear_def();
      break;
    case ScenarioDef::kBarrelDef: {
      def.mutable_barrel_def();
      if (!def.room().has_barrel_room()) {
        *def.mutable_room() = GetDefaultBarrelRoom();
      }
      break;
    }
    case ScenarioDef::kWallArcDef: {
      def.mutable_wall_arc_def();
      def.clear_target_def();
      def.mutable_target_def()->set_num_targets(1);
      auto* p = def.mutable_target_def()->add_profiles();
      p->set_speed(100);
      p->set_acceleration(160);

      def.clear_shot_type();
      def.mutable_shot_type()->set_tracking_invincible(true);
      break;
    }
    case ScenarioDef::kCircleDef:
      def.mutable_circle_def();
      break;
    case ScenarioDef::kSineDef:
      def.mutable_sine_def();
      break;
    case ScenarioDef::kReferenceDef:
      def.clear_level_overrides();
      def.clear_score_targets();
      def.clear_overrides();
      def.mutable_reference_def();
      break;
    case ScenarioDef::TYPE_NOT_SET:
      break;
  }

  if (target_placement.regions_size() > 0) {
    auto maybe_strat = GetTargetPlacementStrategyField(&def);
    if (maybe_strat) {
      *maybe_strat->get_mutable() = target_placement;
    }
  }
}

}  // namespace

void DrawScenarioTypeEditor(ScenarioDef& def,
                            Application* app,
                            std::string* error_message_out,
                            bool* editing_room,
                            ImGui::MultilineTextEntryDialog* description_dialog) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("ScenarioTypeEditor");
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Scenario type");
  ImGui::SameLine();

  if (def.type_case() == ScenarioDef::TYPE_NOT_SET) {
    InitializeScenarioType(def, ScenarioDef::kStaticDef);
  }

  auto scenario_type = def.type_case();
  bool is_new_type = ImGui::SimpleTypeDropdown(
      "ScenarioTypeDropdown", &scenario_type, kScenarioTypes, char_x * 15);
  if (is_new_type) {
    InitializeScenarioType(def, scenario_type);
  }

  ImGui::SpacedSeparator();

  if (scenario_type == ScenarioDef::kReferenceDef) {
    DrawReferenceEditor(def, app, error_message_out, editing_room, description_dialog);
  }
  if (scenario_type == ScenarioDef::kStaticDef) {
    DrawStaticEditor(*def.mutable_static_def());
  }
  if (scenario_type == ScenarioDef::kWaypointDef) {
    DrawWaypointEditor(*def.mutable_waypoint_def());
  }
  if (scenario_type == ScenarioDef::kCenteringDef) {
    DrawCenteringEditor(*def.mutable_centering_def());
  }
  if (scenario_type == ScenarioDef::kAngleStrafeDef) {
    DrawAngleStrafeEditor(*def.mutable_angle_strafe_def());
  }
  if (scenario_type == ScenarioDef::kStrafeDef) {
    DrawStrafeEditor(*def.mutable_strafe_def());
  }
  if (scenario_type == ScenarioDef::kBounceDef) {
    DrawBounceEditor(*def.mutable_bounce_def());
  }
  if (scenario_type == ScenarioDef::kLinearDef) {
    DrawLinearEditor(*def.mutable_linear_def());
  }
  if (scenario_type == ScenarioDef::kBarrelDef) {
    DrawBarrelEditor(def);
  }
  if (scenario_type == ScenarioDef::kWallArcDef) {
    DrawWallArcEditor(*def.mutable_wall_arc_def());
  }
  if (scenario_type == ScenarioDef::kWallWanderDef) {
    DrawWallWanderEditor(*def.mutable_wall_wander_def());
  }
  if (scenario_type == ScenarioDef::kCircleDef) {
    DrawCircleEditor(*def.mutable_circle_def());
  }
  if (scenario_type == ScenarioDef::kSineDef) {
    DrawSineEditor(*def.mutable_sine_def());
  }
}

void DrawSecondaryScenarioTypeEditor(ScenarioDef& def) {
  std::optional<PtrField<TargetPlacementStrategy>> strat;
  if (def.has_strafe_def()) {
    strat = PROTO_PTR_FIELD(TargetPlacementStrategy,
                            StrafeScenarioDef,
                            def.mutable_strafe_def(),
                            target_placement_strategy);
  } else if (def.has_bounce_def()) {
    strat = PROTO_PTR_FIELD(TargetPlacementStrategy,
                            BounceScenarioDef,
                            def.mutable_bounce_def(),
                            target_placement_strategy);
  } else if (def.has_wall_wander_def()) {
    strat = PROTO_PTR_FIELD(TargetPlacementStrategy,
                            WallWanderScenarioDef,
                            def.mutable_wall_wander_def(),
                            target_placement_strategy);
  } else if (def.has_angle_strafe_def()) {
    strat = PROTO_PTR_FIELD(TargetPlacementStrategy,
                            AngleStrafeScenarioDef,
                            def.mutable_angle_strafe_def(),
                            target_placement_strategy);
  }

  if (!strat.has_value()) {
    return;
  }

  bool use_target_placement = strat->has();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Set initial target location");
  ImGui::SameLine();
  bool changed = ImGui::Checkbox("##UseTargetPlacement", &use_target_placement);
  if (use_target_placement) {
    ImGui::Indent();
    DrawTargetPlacementStrategyEditor("Placement", strat->get_mutable());
    ImGui::Unindent();
  } else if (changed) {
    strat->clear();
  }
}

void DrawShotTypeEditor(ShotType& s) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("ShotTypeEditor");

  if (s.type_case() == ShotType::TYPE_NOT_SET) {
    s.set_click_single(true);
  }
  ShotType::TypeCase type = s.type_case();

  if (ImGui::SimpleTypeDropdown("ShotTypeDropdown", &type, kShotTypes, char_x * 15)) {
    s.Clear();
    if (type == ShotType::kClickSingle) {
      s.set_click_single(true);
    }
    if (type == ShotType::kClickMulti) {
      s.set_click_multi(true);
      s.set_health_clicks(3);
    }
    if (type == ShotType::kTrackingInvincible) {
      s.set_tracking_invincible(true);
    }
    if (type == ShotType::kTrackingProximity) {
      s.set_tracking_proximity(true);
    }
    if (type == ShotType::kTrackingKill) {
      s.set_tracking_kill(true);
      s.set_health_seconds(0.4);
    }
    if (type == ShotType::kPoke) {
      s.set_poke(true);
    }
    if (type == ShotType::kPokeInstant) {
      s.set_poke_instant(true);
    }
  }

  if (type == ShotType::kPoke) {
    ImGui::InputFloat(ImGui::InputFloatParams("PokeKillTime")
                          .set_label("Poke kill time")
                          .set_step(0.01, 0.1)
                          .set_min(0.01)
                          .set_default(0.1)
                          .set_is_optional()
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, poke_kill_time_seconds));
  }

  if (type == ShotType::kClickMulti) {
    ImGui::InputInt(ImGui::InputIntParams("ClickCount")
                        .set_label("Clicks to kill")
                        .set_step(1, 2)
                        .set_min(2)
                        .set_default(3)
                        .set_width(char_x * 10),
                    PROTO_INT_FIELD(ShotType, &s, health_clicks));
  }

  if (type == ShotType::kClickMulti || type == ShotType::kClickSingle) {
    ImGui::InputBool("Remove on miss", PROTO_BOOL_FIELD(ShotType, &s, remove_closest_on_miss));
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Ghost on miss")
                          .set_is_optional()
                          .set_step(0.05, 0.2)
                          .set_min(0.05)
                          .set_default(1)
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, ghost_closest_on_miss));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "The closest target will become a ghost on miss and be removed after the specified time");
    if (s.has_ghost_closest_on_miss()) {
      ImGui::Indent();
      ImGui::InputBool("Unghost instead of remove",
                       PROTO_BOOL_FIELD(ShotType, &s, unghost_miss_on_expiration));
      ImGui::SameLine();
      ImGui::HelpMarker(
          "When the time expires the target will become clickable agazin instead of being removed");
      ImGui::Unindent();
    } else {
      s.clear_unghost_miss_on_expiration();
    }

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Click rate")
                          .set_is_optional()
                          .set_step(0.05, 0.2)
                          .set_min(0.05)
                          .set_default(0.5)
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, click_rate_seconds));
    ImGui::SameLine();
    ImGui::HelpMarker("The amount of time in seconds after shooting before you can shoot again");

    bool has_reload = s.has_reload();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Reload");
    ImGui::SameLine();
    ImGui::Checkbox("##ReloadCheckbox", &has_reload);
    ImGui::SameLine();
    ImGui::HelpMarker(
        "There is no accuracy penalty but you will have to reload if you miss too many consecutive "
        "shots");
    if (has_reload) {
      s.set_accuracy_penalty(AccuracyPenalty::ACCURACY_PENALTY_NONE);
      ImGui::Indent();
      ImGui::InputInt(ImGui::InputIntParams::WithLabelAsId("Max shots")
                          .set_step(1, 1)
                          .set_default(3)
                          .set_min(1)
                          .set_width(char_x * 10),
                      PROTO_INT_FIELD(ReloadInfo, s.mutable_reload(), max_shots));
      ImGui::InputInt(ImGui::InputIntParams::WithLabelAsId("Reload on hit")
                          .set_step(1, 1)
                          .set_default(3)
                          .set_min(1)
                          .set_width(char_x * 10),
                      PROTO_INT_FIELD(ReloadInfo, s.mutable_reload(), num_to_reload_on_hit));
      ImGui::SameLine();
      ImGui::HelpMarker("Number of shots to reload on a hit up to the \"max shots\".");

      ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Reload time")
                            .set_step(0.05, 0.2)
                            .set_min(0.01)
                            .set_default(0.5)
                            .set_width(char_x * 10),
                        PROTO_FLOAT_FIELD(ReloadInfo, s.mutable_reload(), reload_time));
      ImGui::Unindent();

    } else {
      s.clear_reload();
      s.set_accuracy_penalty(AccuracyPenalty::ACCURACY_PENALTY_SQRT);
    }
  } else {
    // Not clicking.
    s.clear_click_rate_seconds();
    s.clear_remove_closest_on_miss();
    s.clear_reload();
    s.clear_accuracy_penalty();
  }

  if (type == ShotType::kTrackingKill) {
    ImGui::InputFloat(ImGui::InputFloatParams("HealthSeconds")
                          .set_label("Health time")
                          .set_step(0.01, 0.1)
                          .set_min(0.01)
                          .set_default(0.4)
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, health_seconds));
    ImGui::SameLine();
    ImGui::HelpMarker("The amount of time in seconds to kill the target.");

    ImGui::InputFloat(ImGui::InputFloatParams("HealthRegenRate")
                          .set_label("Health regen rate")
                          .set_step(0.1, 0.5)
                          .set_min(0.1)
                          .set_default(1)
                          .set_is_optional()
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, health_regen_rate));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "The rate health is regenerated if you switch off target before killing. 1 means regen "
        "at same rate as health is taken away for hits.");

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Health forgiveness time")
                          .set_step(0.01, 0.05)
                          .set_min(0.01)
                          .set_max(5)
                          .set_default(0.10)
                          .set_is_optional()
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, remove_if_below_health_seconds));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "If the target has less than the specified amount of health left (in seconds) and you move "
        "off the target, it will be removed and you will get partial points. The kill sound is "
        "played when this threshold is passed.");

    ImGui::InputBool("No partial kills", PROTO_BOOL_FIELD(ShotType, &s, no_partial_kills));
  }
}

}  // namespace aim
