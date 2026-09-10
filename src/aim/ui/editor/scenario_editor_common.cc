#include "scenario_editor_common.h"

#include <functional>

#include "aim/common/field.h"
#include "aim/common/imgui_ext.h"
#include "aim/ui/editor/profile_list_editor.h"
#include "imgui.h"

namespace aim {
namespace {

template <typename T>
std::unordered_map<T, std::string> BuildDisplayNameMap(
    const std::vector<std::pair<T, std::string>>& values) {
  std::unordered_map<T, std::string> result;
  for (const auto& entry : values) {
    result[entry.first] = entry.second;
  }
  return result;
}

void DrawTargetRegion(float char_x, bool support_depth, TargetRegion* region) {
  if (region->type_case() == TargetRegion::TYPE_NOT_SET) {
    region->mutable_rectangle();
  }
  auto region_type = region->type_case();
  ImGui::SimpleTypeDropdown("RegionTypeDropdown", &region_type, kRegionTypes, char_x * 15);

  bool is_point = region_type == TargetRegion::kPoint;
  if (is_point) {
    DrawRegionVec2Editor("Point", region->mutable_point());
  }

  if (region_type == TargetRegion::kRectangle) {
    auto* t = region->mutable_rectangle();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Width");
    ImGui::SameLine();
    DrawRegionLengthEditor("Width", RegionLength::kXPercentValue, t->mutable_x_length(), 90);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Height");
    ImGui::SameLine();
    DrawRegionLengthEditor("Height", RegionLength::kYPercentValue, t->mutable_y_length(), 90);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Exclude inner rectangle");
    ImGui::SameLine();
    bool use_inner = t->has_inner_x_length() || t->has_inner_y_length();
    ImGui::Checkbox("##InnerCheckbox", &use_inner);
    if (use_inner) {
      ImGui::IdGuard lid("InnerInputs");
      ImGui::Indent();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Width");
      ImGui::SameLine();
      DrawRegionLengthEditor(
          "InnerWidth", RegionLength::kXPercentValue, t->mutable_inner_x_length(), 25);

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Height");
      ImGui::SameLine();
      DrawRegionLengthEditor(
          "InnerHeight", RegionLength::kYPercentValue, t->mutable_inner_y_length(), 25);

      ImGui::Unindent();
    } else {
      t->clear_inner_x_length();
      t->clear_inner_y_length();
    }
  }

  if (region_type == TargetRegion::kCircle) {
    auto* t = region->mutable_circle();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Diameter");
    ImGui::SameLine();
    DrawRegionLengthEditor("Diameter", RegionLength::kXPercentValue, t->mutable_diameter(), 90);

    // TODO: Optional region length
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Exclude inner diameter");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor(
        "InnerDiameter",
        RegionLength::kXPercentValue,
        PROTO_PTR_FIELD(RegionLength, CircleTargetRegion, t, inner_diameter),
        25);
  }

  if (region_type == TargetRegion::kEllipse) {
    auto* t = region->mutable_ellipse();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("X diameter");
    ImGui::SameLine();
    DrawRegionLengthEditor("XDiameter", RegionLength::kXPercentValue, t->mutable_x_diameter(), 90);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Y diameter");
    ImGui::SameLine();
    DrawRegionLengthEditor("YDiameter", RegionLength::kYPercentValue, t->mutable_y_diameter(), 90);
  }

  if (support_depth) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Depth");
    ImGui::SameLine();
    DrawOptionalJitteredRegionLengthEditor(
        "Depth",
        RegionLength::kDepthPercentValue,
        PROTO_PTR_FIELD(RegionLength, TargetRegion, region, depth),
        PROTO_PTR_FIELD(RegionLength, TargetRegion, region, depth_jitter),
        0);
    ImGui::SameLine();
    ImGui::HelpMarker(
        "The distance away from the wall towards the camera. The greater the value, the "
        "further it is from the wall.");
  } else {
    region->clear_depth();
    region->clear_depth_jitter();
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Fixed distance");
  ImGui::SameLine();
  DrawOptionalJitteredRegionLengthEditor(
      "FixedDistanceInput",
      RegionLength::kXPercentValue,
      PROTO_PTR_FIELD(RegionLength, TargetRegion, region, fixed_distance_from_last_target),
      PROTO_PTR_FIELD(RegionLength, TargetRegion, region, fixed_distance_from_last_target_jitter),
      10);
  ImGui::SameLine();
  ImGui::HelpMarker(
      "New target will be placed at a fixed distance from the last target that was added.");

  if (!is_point) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Offset");
    ImGui::SameLine();
    bool use_offsets = region->has_x_offset() || region->has_y_offset();
    ImGui::Checkbox("##OffsetsCheckbox", &use_offsets);
    if (use_offsets) {
      ImGui::Indent();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("X offset");
      ImGui::SameLine();
      DrawRegionLengthPointEditor(
          "XOffset", RegionLength::kXPercentValue, region->mutable_x_offset());

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Y offset");
      ImGui::SameLine();
      DrawRegionLengthPointEditor(
          "YOffset", RegionLength::kYPercentValue, region->mutable_y_offset());
      ImGui::Unindent();
    } else {
      region->clear_x_offset();
      region->clear_y_offset();
    }
  } else {
    // No offset allowed. Ensure it is cleared.
    region->clear_x_offset();
    region->clear_y_offset();
  }
}

}  // namespace

void SetRegionLengthValue(RegionLength* length, RegionLength::TypeCase type, float value) {
  switch (type) {
    case RegionLength::kValue:
      length->set_value(value);
      return;
    case RegionLength::kDepthPercentValue:
      length->set_depth_percent_value(value / 100.0f);
      return;
    case RegionLength::kXPercentValue:
      length->set_x_percent_value(value / 100.0f);
      return;
    case RegionLength::kYPercentValue:
      length->set_y_percent_value(value / 100.0f);
      return;
    case RegionLength::TYPE_NOT_SET:
      return;
  }
}

float GetRegionLengthValue(RegionLength* length) {
  switch (length->type_case()) {
    case RegionLength::kValue:
      return length->value();
    case RegionLength::kDepthPercentValue:
      return length->depth_percent_value() * 100;
    case RegionLength::kXPercentValue:
      return length->x_percent_value() * 100;
    case RegionLength::kYPercentValue:
      return length->y_percent_value() * 100;
    case RegionLength::TYPE_NOT_SET:
      return 0;
  }
  return 0;
}

void DrawRegionLengthEditor(const std::string& id,
                            RegionLength::TypeCase default_type,
                            RegionLength* length,
                            float default_value,
                            bool is_point) {
  float char_x = ImGui::GetDefaultCharSizeX();
  float value = GetRegionLengthValue(length);
  RegionLength::TypeCase type = length->type_case();
  if (type == RegionLength::TYPE_NOT_SET) {
    type = default_type;
    value = default_value;
  }
  ImGui::IdGuard cid(id);

  Field<float> field = CreateFloatField(&value);
  auto params = ImGui::InputFloatParams("ValueInput").set_step(1, 5).set_width(char_x * 9);
  if (!is_point) {
    params.set_min(0);
  }
  ImGui::InputFloat(params, field);

  ImGui::SameLine();
  bool type_changed =
      ImGui::SimpleTypeDropdown("TypeDropdown", &type, kRegionLengthTypes, char_x * 8);

  SetRegionLengthValue(length, type, field.get());
}

void DrawRegionLengthPointEditor(const std::string& id,
                                 RegionLength::TypeCase default_type,
                                 RegionLength* length) {
  return DrawRegionLengthEditor(id, default_type, length, 0, /*is_point=*/true);
}

void DrawJitteredRegionLengthEditor(const std::string& id,
                                    RegionLength::TypeCase default_type,
                                    RegionLength* length,
                                    RegionLength* jitter_length,
                                    float default_value) {
  float char_x = ImGui::GetDefaultCharSizeX();
  float value = GetRegionLengthValue(length);
  float jitter_value = GetRegionLengthValue(jitter_length);
  RegionLength::TypeCase type = length->type_case();
  if (type == RegionLength::TYPE_NOT_SET) {
    type = default_type;
    value = default_value;
  }
  ImGui::IdGuard cid(id);

  Field<float> field = CreateFloatField(&value);
  Field<float> jitter_field = CreateFloatField(&jitter_value);

  auto params = ImGui::InputFloatParams("ValueInput").set_step(1, 5).set_width(char_x * 9);

  ImGui::InputFloat(params, field);
  ImGui::SameLine();
  ImGui::Text("+/-");
  ImGui::SameLine();
  params.set_id("JitteredValueInput").set_step(0.5, 2.5);
  ImGui::InputFloat(params, jitter_field);

  ImGui::SameLine();
  bool type_changed =
      ImGui::SimpleTypeDropdown("TypeDropdown", &type, kRegionLengthTypes, char_x * 8);

  SetRegionLengthValue(length, type, field.get());
  SetRegionLengthValue(jitter_length, type, jitter_field.get());
}

void DrawOptionalJitteredRegionLengthEditor(const std::string& id,
                                            RegionLength::TypeCase default_type,
                                            PtrField<RegionLength> length,
                                            PtrField<RegionLength> jitter_length,
                                            float default_value) {
  ImGui::IdGuard cid(id);
  bool has_value = length.has();
  ImGui::Checkbox("##UseRegionLength", &has_value);
  if (has_value) {
    ImGui::SameLine();
    DrawJitteredRegionLengthEditor("RegionLength",
                                   default_type,
                                   length.get_mutable(),
                                   jitter_length.get_mutable(),
                                   default_value);
  } else {
    length.clear();
    jitter_length.clear();
  }
}

void DrawOptionalRegionLengthEditor(const std::string& id,
                                    RegionLength::TypeCase default_type,
                                    PtrField<RegionLength> length,
                                    float default_value) {
  ImGui::IdGuard cid(id);
  bool has_value = length.has();
  ImGui::Checkbox("##UseRegionLength", &has_value);
  if (has_value) {
    ImGui::SameLine();
    DrawRegionLengthEditor("RegionLength", default_type, length.get_mutable(), default_value);
  } else {
    length.clear();
  }
}

void DrawRegionVec2Editor(const std::string& id, RegionVec2* v) {
  ImGui::IdGuard cid(id);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("x");
  ImGui::SameLine();
  DrawRegionLengthPointEditor("X" + id, RegionLength::kXPercentValue, v->mutable_x());
  ImGui::AlignTextToFramePadding();
  ImGui::Text("y");
  ImGui::SameLine();
  DrawRegionLengthPointEditor("Y" + id, RegionLength::kYPercentValue, v->mutable_y());
}

void DrawOptionalRegionVec2Editor(const std::string& id, PtrField<RegionVec2> field) {
  ImGui::IdGuard cid(id);

  bool has_value = field.has();
  ImGui::Checkbox("##UseRegionVec2", &has_value);
  if (has_value) {
    ImGui::Indent();
    DrawRegionVec2Editor("RegionVec2", field.get_mutable());
    ImGui::Unindent();
  } else {
    field.clear();
  }
}

void VectorEditor(ImGui::InputFloatParams params, StoredVec3* v) {
  ImGui::IdGuard cid(params.id);

  ImGui::InputFloat(params.set_label("X").set_id("##XInput"), PROTO_FLOAT_FIELD(StoredVec3, v, x));

  ImGui::InputFloat(params.set_label("Y").set_id("##YInput"), PROTO_FLOAT_FIELD(StoredVec3, v, y));

  ImGui::InputFloat(params.set_label("Z").set_id("##ZInput"), PROTO_FLOAT_FIELD(StoredVec3, v, z));
}

void DrawTargetPlacementStrategyEditor(const std::string& id,
                                       TargetPlacementStrategy* s,
                                       bool support_depth) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid(id);
  if (s->regions_size() == 0) {
    s->add_regions();
  }

  ImGui::Text("Target locations");
  ImGui::Indent();
  DrawProfileList("RegionList",
                  "Region",
                  PROTO_PTR_FIELD(ProfileListInfo, TargetPlacementStrategy, s, regions_info),
                  s->mutable_regions(),
                  std::bind_front(&DrawTargetRegion, char_x, support_depth));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Min distance");
  ImGui::SameLine();

  DrawOptionalRegionLengthEditor(
      "MinDistanceInput",
      RegionLength::kXPercentValue,
      PROTO_PTR_FIELD(RegionLength, TargetPlacementStrategy, s, min_distance),
      1);
  ImGui::SameLine();
  ImGui::HelpMarker("Minimum distance between targets.");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Min distance X");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor(
      "MinDistanceXInput",
      RegionLength::kXPercentValue,
      PROTO_PTR_FIELD(RegionLength, TargetPlacementStrategy, s, min_x_distance),
      1);
  ImGui::SameLine();
  ImGui::HelpMarker("Minimum horizontal distance between targets along the x axis.");

  ImGui::AlignTextToFramePadding();
  ImGui::Text("Min distance Y");
  ImGui::SameLine();
  DrawOptionalRegionLengthEditor(
      "MinDistanceYInput",
      RegionLength::kYPercentValue,
      PROTO_PTR_FIELD(RegionLength, TargetPlacementStrategy, s, min_y_distance),
      1);
  ImGui::SameLine();
  ImGui::HelpMarker("Minimum vertical distance between targets along the y axis.");
}

void DrawBoundsEditor(const std::string& id, Bounds* bounds, BoundsDimensions dimensions) {
  ImGui::IdGuard cid(id);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Bounds");
  ImGui::Indent();

  if (dimensions.draw_width) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Width");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("Width",
                                   RegionLength::kXPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, bounds, width),
                                   90);
  }

  if (dimensions.draw_height) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Height");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("Height",
                                   RegionLength::kYPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, bounds, height),
                                   90);
  }

  if (dimensions.draw_depth) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Depth");
    ImGui::SameLine();
    DrawOptionalRegionLengthEditor("Depth",
                                   RegionLength::kDepthPercentValue,
                                   PROTO_PTR_FIELD(RegionLength, Bounds, bounds, depth),
                                   40);
  }
  ImGui::Unindent();
}

void DrawOverridesEditor(const char* id, ScenarioOverrides* overrides, bool is_levels) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::IdGuard cid(id);
  const ImGui::InputFloatParams default_params =
      GetDefaultMultiplierInputParams("Default").set_is_optional().set_width(char_x * 10);
  ImGui::InputFloat(default_params.clone().set_id_and_label("Target radius multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, target_radius_multiplier));
  ImGui::InputFloat(default_params.clone().set_id_and_label("Speed multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, speed_multiplier));
  ImGui::InputFloat(default_params.clone().set_id_and_label("Acceleration multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, acceleration_multiplier));
  /*
  // TODO: Not sure if these are implemented correctly or make sense. Maybe should have more
  // specific names.
  ImGui::InputFloat(default_params.clone().set_id_and_label("Time scale multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, time_scale_multiplier));
  ImGui::InputFloat(default_params.clone().set_id_and_label("Distance multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, distance_multiplier));
  */
  ImGui::InputFloat(default_params.clone().set_id_and_label("Pulse time multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, growth_time_multiplier));
  ImGui::InputFloat(
      default_params.clone().set_id_and_label("Remove after time multiplier"),
      PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, remove_target_after_seconds_multiplier));
  ImGui::InputFloat(
      default_params.clone().set_id_and_label("Min distance between targets multiplier"),
      PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, min_distance_multiplier));
  ImGui::InputFloat(
      default_params.clone().set_id_and_label("Fixed distance from last target multiplier"),
      PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, fixed_distance_from_last_target_multiplier));
  ImGui::InputFloat(default_params.clone().set_id_and_label("Wall width multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, wall_width_multiplier));
  ImGui::InputFloat(default_params.clone().set_id_and_label("Wall height multiplier"),
                    PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, wall_height_multiplier));
  if (is_levels) {
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Level offset")
                          .set_step(1, 2)
                          .set_default(0)
                          .set_is_optional()
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(ScenarioOverrides, overrides, level_offset));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "Add this offset when evaluating levels. With +2 offset L1 is now equivalent to L3 "
        "(without offset). Or think of it as L2 is now the equivalent to the base scenario.");
  }
}

ImGui::InputFloatParams GetDefaultMultiplierInputParams(const std::string& label) {
  return ImGui::InputFloatParams::WithLabelAsId(label)
      .set_step(0.01, 0.25)
      .set_min(0.01)
      .set_default(1);
}

const std::unordered_map<ScenarioDef::TypeCase, std::string> kScenarioTypeDisplayNameMap =
    BuildDisplayNameMap(kScenarioTypes);

const std::unordered_map<ShotType::TypeCase, std::string> kShotTypeDisplayNameMap =
    BuildDisplayNameMap(kShotTypes);

void DrawScoreTargetsEditor(PtrField<ScoreTargets> score_targets) {
  float char_x = ImGui::GetDefaultCharSizeX();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("Target score");
  ImGui::SameLine();
  bool has_score_targets = score_targets.has();
  ImGui::SameLine();
  ImGui::Checkbox("##ScoreTargetsCheckbox", &has_score_targets);
  if (has_score_targets) {
    ScoreTargets* t = score_targets.get_mutable();
    ImGui::SameLine();
    ImGui::InputFloat(ImGui::InputFloatParams("TargetScoreInput")
                          .set_min(0.1)
                          .set_step(0.1, 2)
                          .set_width(char_x * 12),
                      PROTO_FLOAT_FIELD(ScoreTargets, t, target_score));
  } else {
    score_targets.clear();
  }
  ImGui::SameLine();
  ImGui::HelpMarker(
      "Defines a target for scores. Hitting the target gives a 5.0. 95% of target gives a 4.0. "
      "Each 5% goes down 1 more score level.");
}

}  // namespace aim
