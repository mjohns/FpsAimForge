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
#include "aim/ui/editor/strafe_editor.h"
#include "aim/ui/search_selector.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

void DrawWallArcEditor(WallArcScenarioDef& d) {
  ImGui::IdGuard cid("WallArcEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Width"));
  ImGui::SameLine();
  DrawRegionLengthEditor("Width", RegionLength::kXPercentValue, d.mutable_width(), 50);
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("The arc will be stretched over the specified width"));

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Height"));
  ImGui::SameLine();
  DrawRegionLengthEditor("Height", RegionLength::kYPercentValue, d.mutable_height(), 50);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Height range"));
  ImGui::SameLine();
  bool use_range = d.has_height_jitter();
  ImGui::Checkbox("##UseRange", &use_range);
  if (use_range) {
    ImGui::SameLine();
    DrawRegionLengthEditor("Height range", RegionLength::kYPercentValue, d.mutable_height_jitter());
  } else {
    d.clear_height_jitter();
  }

  ImGui::InputBool(Tr("Reflect"), PROTO_BOOL_FIELD(WallArcScenarioDef, &d, reflect));
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Turn the arc upside down."));

  ImGui::InputBool(Tr("Start on floor"), PROTO_BOOL_FIELD(WallArcScenarioDef, &d, start_on_ground));
}

void DrawSineEditor(SineScenarioDef& d) {
  ImGui::IdGuard cid("SineEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Height"));
  ImGui::SameLine();
  DrawRegionLengthEditor("Height", RegionLength::kYPercentValue, d.mutable_height(), 20);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Width"));
  ImGui::SameLine();
  DrawRegionLengthEditor("Width", RegionLength::kXPercentValue, d.mutable_width(), 20);

  ImGui::InputBool(Tr("Going left"), PROTO_BOOL_FIELD(SineScenarioDef, &d, going_left));
}

void DrawCircleEditor(CircleScenarioDef& d) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("CircleEditor");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Radius"));
  ImGui::SameLine();
  DrawRegionLengthEditor("Radius", RegionLength::kXPercentValue, d.mutable_radius(), 30);

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Final radius"));
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
  ImGui::HelpMarker(Tr("0 degrees starts at 3 o'clock and rotates counter clockwise."));

  ImGui::InputBool(Tr("Start clockwise"), PROTO_BOOL_FIELD(CircleScenarioDef, &d, rotate_clockwise));

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
  ImGui::Text("%s", Tr("Depth"));
  ImGui::SameLine();
  DrawRegionLengthEditor("Depth", RegionLength::kDepthPercentValue, d.mutable_depth(), 0);
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Distance away from the wall"));
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
  ImGui::HelpMarker(Tr("The amount of time to turn in a single direction before switching."));

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
      "between turns base on turn time.");
}

void DrawWallWanderEditor(WallWanderScenarioDef& d) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("WallWanderEditor");

  if (d.profiles_size() == 0) {
    d.add_profiles();
  }
  ImGui::Text("%s", Tr("Wander profiles"));
  ImGui::Indent();
  DrawProfileList("WanderProfileList",
                  "Profile",
                  PROTO_PTR_FIELD(ProfileListInfo, WallWanderScenarioDef, &d, profiles_info),
                  d.mutable_profiles(),
                  std::bind_front(&DrawWallWanderProfile, char_x));
  ImGui::Unindent();
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
  ImGui::Text("%s", Tr("Initial left/right direction"));
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
  ImGui::Text("%s", Tr("Initial up/down direction"));
  ImGui::SameLine();
  Direction up_down_direction_type = d.up_down_initial_direction();
  ImGui::SimpleTypeDropdown(
      "UpDownDirectionTypeDropdown", &up_down_direction_type, kUpDownDirections, char_x * 20);
  d.set_up_down_initial_direction(up_down_direction_type);
}

void DrawBarrelEditor(ScenarioDef& def) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid("BarrelEditor");
  BarrelScenarioDef& d = *def.mutable_barrel_def();

  if (!def.room().has_barrel_room()) {
    ImGui::Text("%s", Tr("Must use barrel room"));
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
}

void DrawBounceProfile(float char_x, BounceProfile* p) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Bounce height"));
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
  ImGui::InputBool(Tr("Only delay on floor"), PROTO_BOOL_FIELD(BounceProfile, p, only_delay_on_floor));

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
  ImGui::Text("%s", Tr("Floor height"));
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor("FloorHeight",
                                 RegionLength::kYPercentValue,
                                 PROTO_PTR_FIELD(RegionLength, BounceScenarioDef, &d, floor_height),
                                 0);
  ImGui::InputBool(Tr("Start on floor"), PROTO_BOOL_FIELD(BounceScenarioDef, &d, start_on_floor));

  ImGui::SpacedSeparator();

  if (d.bounce_profiles_size() == 0) {
    d.add_bounce_profiles();
  }
  ImGui::Text("%s", Tr("Bounce profiles"));
  ImGui::Indent();
  DrawProfileList("BounceProfileList",
                  "Profile",
                  PROTO_PTR_FIELD(ProfileListInfo, BounceScenarioDef, &d, bounce_profiles_info),
                  d.mutable_bounce_profiles(),
                  std::bind_front(&DrawBounceProfile, char_x));
  ImGui::Unindent();
}

void DrawAngleStrafeProfile(float char_x, AngleStrafeProfile* p) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Distance"));
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

  ImGui::Text("%s", Tr("Strafe profiles"));
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
  ImGui::Text("%s", Tr("Type"));
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
    ImGui::Text("%s", Tr("Point 1"));
    ImGui::Indent();
    DrawRegionVec2Editor("Point1", c.mutable_wall_points(0));
    ImGui::Unindent();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Point 2"));
    ImGui::Indent();
    DrawRegionVec2Editor("Point2", c.mutable_wall_points(1));
    ImGui::Unindent();

    int remove_at_i = -1;
    for (int i = 2; i < c.wall_points_size(); ++i) {
      ImGui::IdGuard lid("ExtraPoint", i);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", TrFormat("Point {}", i + 1).c_str());
      ImGui::SameLine();
      if (ImGui::Button(icons::kCancel)) {
        remove_at_i = i;
      }
      ImGui::Indent();
      DrawRegionVec2Editor("##PointEditor", c.mutable_wall_points(i));
      ImGui::Unindent();
    }

    ImGui::Spacing();
    if (ImGui::Button(Tr("Add point"))) {
      c.add_wall_points();
    }

    if (remove_at_i > 0) {
      c.mutable_wall_points()->erase(c.mutable_wall_points()->begin() + remove_at_i);
    }
  }
}

}  // namespace

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
    ImGui::InputBool(Tr("Remove on miss"), PROTO_BOOL_FIELD(ShotType, &s, remove_closest_on_miss));
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Ghost on miss")
                          .set_is_optional()
                          .set_step(0.05, 0.2)
                          .set_min(0.05)
                          .set_default(1)
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ShotType, &s, ghost_closest_on_miss));
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("The closest target will become a ghost on miss and be removed after the specified time"));
    if (s.has_ghost_closest_on_miss()) {
      ImGui::Indent();
      ImGui::InputBool(Tr("Unghost instead of remove"),
                       PROTO_BOOL_FIELD(ShotType, &s, unghost_miss_on_expiration));
      ImGui::SameLine();
      ImGui::HelpMarker(Tr("When the time expires the target will become clickable agazin instead of being removed"));
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
    ImGui::HelpMarker(Tr("The amount of time in seconds after shooting before you can shoot again"));

    bool has_reload = s.has_reload();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Reload"));
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
      ImGui::HelpMarker(Tr("Number of shots to reload on a hit up to the \"max shots\"."));

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
    ImGui::HelpMarker(Tr("The amount of time in seconds to kill the target."));

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

    ImGui::InputBool(Tr("No partial kills"), PROTO_BOOL_FIELD(ShotType, &s, no_partial_kills));
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
      break;
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

void DrawScenarioTypeTabs(ScenarioDef& def, Application* app) {
  auto scenario_type = def.type_case();
  if (scenario_type == ScenarioDef::kStrafeDef) {
    auto* d = def.mutable_strafe_def();
    DrawStrafeTabs(def);
    if (IsDefaultInstance(d->relative_bounds())) {
      d->clear_relative_bounds();
    }
    if (IsDefaultInstance(d->bounds())) {
      d->clear_bounds();
    }
  }
  if (scenario_type == ScenarioDef::kCenteringDef) {
    if (ImGui::BeginTabItem(Tr("Centering"))) {
      ImGui::BeginChild("CenteringContent");
      DrawCenteringEditor(*def.mutable_centering_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  if (scenario_type == ScenarioDef::kAngleStrafeDef) {
    if (ImGui::BeginTabItem("Angle strafe")) {
      ImGui::BeginChild("StrafeContent");
      ImGui::Spacing();
      DrawAngleStrafeEditor(*def.mutable_angle_strafe_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    DrawStrafeTabs(def);
  }
  if (scenario_type == ScenarioDef::kBounceDef) {
    auto* d = def.mutable_bounce_def();
    if (ImGui::BeginTabItem(Tr("Bounce"))) {
      ImGui::BeginChild("BounceContent");
      ImGui::Spacing();
      DrawBounceEditor(*d);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    DrawStrafeTabs(def);
    if (IsDefaultInstance(d->relative_bounds())) {
      d->clear_relative_bounds();
    }
    if (IsDefaultInstance(d->bounds())) {
      d->clear_bounds();
    }
  }
  if (scenario_type == ScenarioDef::kLinearDef) {
    if (ImGui::BeginTabItem(Tr("Linear"))) {
      ImGui::BeginChild("LinearContent");
      ImGui::Spacing();
      DrawLinearEditor(*def.mutable_linear_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  if (scenario_type == ScenarioDef::kBarrelDef) {
    if (ImGui::BeginTabItem(Tr("Barrel"))) {
      ImGui::BeginChild("BarrelContent");
      ImGui::Spacing();
      DrawBarrelEditor(def);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  if (scenario_type == ScenarioDef::kWallArcDef) {
    if (ImGui::BeginTabItem("Arc")) {
      ImGui::BeginChild("WallArcContent");
      ImGui::Spacing();
      DrawWallArcEditor(*def.mutable_wall_arc_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  if (scenario_type == ScenarioDef::kWallWanderDef) {
    if (ImGui::BeginTabItem("Wander")) {
      ImGui::BeginChild("WallWanderContent");
      ImGui::Spacing();
      DrawWallWanderEditor(*def.mutable_wall_wander_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  if (scenario_type == ScenarioDef::kCircleDef) {
    if (ImGui::BeginTabItem(Tr("Circle"))) {
      ImGui::BeginChild("CircleContent");
      ImGui::Spacing();
      DrawCircleEditor(*def.mutable_circle_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  if (scenario_type == ScenarioDef::kSineDef) {
    if (ImGui::BeginTabItem(Tr("Sine"))) {
      ImGui::BeginChild("SineContent");
      ImGui::Spacing();
      DrawSineEditor(*def.mutable_sine_def());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
}

void DrawReferenceEditor(ScenarioDef& def,
                         Application* app,
                         std::string* error_message_out,
                         bool* editing_room) {
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
  ImGui::Text("%s", Tr("Scenario"));
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("The name of the scenario to reference"));
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
      ImGui::HelpTooltip(Tr("Go to the referenced scenario. All current edits will be lost."));
    }
  } else {
    ImGui::InputText("##ScenarioReference", r.mutable_scenario_name());
  }

  ImGui::SpacedSeparator();

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
    ImGui::Text("%s", Tr("Change room"));
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

  if (app != nullptr) {
    if (ImGui::Button(Tr("Bake"))) {
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
    ImGui::HelpMarker(Tr("Expand and remove the reference. Will now be an equivalent normal scenario"));
  }
}

}  // namespace aim
