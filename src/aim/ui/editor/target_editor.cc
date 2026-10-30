#include "target_editor.h"

#include <functional>
#include <string>

#include "aim/common/field.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/proto/scenario.pb.h"
#include "aim/ui/editor/profile_list_editor.h"
#include "imgui.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

// Types that don't require speed to be set.
const std::unordered_set<ScenarioDef::TypeCase> kNoSpeedScenarioTypes = {
    ScenarioDef::kStaticDef,
    ScenarioDef::kReferenceDef,
};

// These types don't support acceleration (yet).
const std::unordered_set<ScenarioDef::TypeCase> kAccelerationDisabledScenarioTypes = {
    ScenarioDef::kLinearDef,
    ScenarioDef::kBarrelDef,
    ScenarioDef::kWallWanderDef,
    ScenarioDef::kCircleDef,
    ScenarioDef::kSineDef,
};

void DrawTargetProfile(float char_x, ScenarioDef& def, TargetProfile* profile) {
  ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Radius")
                                .set_step(0.05, 0.5)
                                .set_min(0.01)
                                .set_default(2)
                                .set_width(char_x * 10),
                            PROTO_JITTERED_FIELD(TargetProfile, profile, target_radius));

  if (kNoSpeedScenarioTypes.contains(def.type_case())) {
    profile->clear_speed();
    profile->clear_acceleration();
  } else {
    ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Speed")
                                  .set_step(1, 10)
                                  .set_min(0.1)
                                  .set_default(100)
                                  .set_zero_is_unset()
                                  .set_width(char_x * 10),
                              PROTO_JITTERED_FIELD(TargetProfile, profile, speed));

    if (kAccelerationDisabledScenarioTypes.contains(def.type_case())) {
      profile->clear_acceleration();
    } else {
      ImGui::InputJitteredFloat(ImGui::InputFloatParams::WithLabelAsId("Acceleration")
                                    .set_step(5, 50)
                                    .set_min(1)
                                    .set_default(200)
                                    .set_is_optional()
                                    .set_width(char_x * 10),
                                PROTO_JITTERED_FIELD(TargetProfile, profile, acceleration));
    }
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Use pill shape"));
  ImGui::SameLine();
  bool use_pill = profile->has_pill();
  ImGui::Checkbox("##UsePill", &use_pill);
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Switch from sphere target to a pill (capsule) shaped target."));
  if (use_pill) {
    ImGui::Indent();
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Height")
                          .set_default(20)
                          .set_min(0.1)
                          .set_width(char_x * 12)
                          .set_step(0.5, 1),
                      PROTO_FLOAT_FIELD(PillTargetDef, profile->mutable_pill(), height));
    ImGui::Unindent();
  } else {
    profile->clear_pill();
  }

  bool has_growth = profile->target_radius_growth_time_seconds() > 0;
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Pulse"));
  ImGui::SameLine();
  ImGui::Checkbox("##PulseCheckbox", &has_growth);
  ImGui::SameLine();
  ImGui::HelpMarker(
      "Target will grow to a certain size over some duration. If it is not killed by "
      "then, it will be removed.");
  if (has_growth) {
    ImGui::Indent();
    ImGui::InputFloat(ImGui::InputFloatParams("GrowthTime")
                          .set_label("Time seconds")
                          .set_default(2)
                          .set_width(char_x * 10)
                          .set_step(0.1, 0.5)
                          .set_min(0.1),
                      PROTO_FLOAT_FIELD(TargetProfile, profile, target_radius_growth_time_seconds));

    ImGui::InputFloat(ImGui::InputFloatParams("GrowthRadius")
                          .set_label("Final radius")
                          .set_default(3)
                          .set_width(char_x * 10)
                          .set_step(0.1, 0.5)
                          .set_min(0.01),
                      PROTO_FLOAT_FIELD(TargetProfile, profile, target_radius_growth_size));

    ImGui::InputFloat(
        ImGui::InputFloatParams::WithLabelAsId("Time at final size")
            .set_step(0.1, 0.5)
            .set_min(0)
            .set_default(0)
            .set_is_optional()
            .set_width(char_x * 10),
        PROTO_FLOAT_FIELD(TargetProfile, profile, target_radius_growth_final_size_time_seconds));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "Time target will stay at final size before being removed. Defaults to 0. i.e. remove "
        "immediately upon reaching final size. This time is in addition to the pulse time.");

    ImGui::InputFloat(
        ImGui::InputFloatParams::WithLabelAsId("Acceleration factor")
            .set_step(0.05, 0.1)
            .set_min(0)
            .set_max(1)
            .set_default(0.5)
            .set_is_optional()
            .set_width(char_x * 10),
        PROTO_FLOAT_FIELD(TargetProfile, profile, target_radius_growth_acceleration_factor));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "The rate of radius size change will increase over time (acceleration). 0 is default "
        "constant growth rate - same as unset. 1 is max acceleration meaning the growth rate will "
        "be highest possible right at end and 0 at start.");

    ImGui::InputJitteredFloat(
        ImGui::InputFloatParams::WithLabelAsId("Initial delay seconds")
            .set_is_optional(true)
            .set_step(0.1, 0.5)
            .set_min(0.01)
            .set_default(0.5)
            .set_width(char_x * 10),
        PROTO_JITTERED_FIELD(TargetProfile, profile, target_radius_growth_initial_delay_seconds));
    ImGui::SameLine();
    ImGui::HelpMarker("Time spent at initial radius size before size change starts.");
    ImGui::Unindent();
  } else {
    profile->clear_target_radius_growth_time_seconds();
    profile->clear_target_radius_growth_final_size_time_seconds();
    profile->clear_target_radius_growth_size();
    profile->clear_target_radius_growth_acceleration_factor();
    profile->clear_target_radius_growth_initial_delay_seconds();
    profile->clear_target_radius_growth_initial_delay_seconds_jitter();
  }

  if (def.shot_type().type_case() == ShotType::kClickMulti ||
      def.shot_type().type_case() == ShotType::kTrackingKill) {
    ImGui::InputFloat(ImGui::InputFloatParams("TargetRadiusAtill")
                          .set_label("Target radius at kill")
                          .set_step(0.1, 0.5)
                          .set_min(0.1)
                          .set_default(profile->target_radius())
                          .set_is_optional()
                          .set_width(char_x * 10),
                      PROTO_FLOAT_FIELD(TargetProfile, profile, target_radius_at_kill));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "The radius of the target will change to the specified value incrementally based on "
        "how much health remains");
  } else {
    profile->clear_target_radius_at_kill();
  }
}

}  // namespace

void DrawTargetEditor(ScenarioDef& def) {
  ImGui::IdGuard cid("TargetEditor");
  TargetDef* t = def.mutable_target_def();

  float char_x = ImGui::GetDefaultCharSizeX();

  int num_targets = t->num_targets();
  if (num_targets <= 0) {
    num_targets = 1;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Number of targets"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(char_x * 8);
  ImGui::InputInt("##NumberEntry", &num_targets, 1, 1);
  t->set_num_targets(num_targets);

  if (t->profiles_size() == 0) {
    t->add_profiles();
  }

  ImGui::SpacedSeparator();

  ImGui::Text("%s", Tr("Target profiles"));
  ImGui::Indent();
  DrawProfileList("ProfileList",
                  "Profile",
                  PROTO_PTR_FIELD(ProfileListInfo, TargetDef, t, profiles_info),
                  t->mutable_profiles(),
                  std::bind_front(&DrawTargetProfile, char_x, def));
  ImGui::Unindent();

  ImGui::SpacedSeparator();

  ImGui::InputBool(Tr("Newest target is ghost"),
                   PROTO_BOOL_FIELD(TargetDef, t, newest_target_is_ghost));
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Ghost targets are unkillable and drawn in a different color."));

  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Ghost wall border")
                        .set_is_optional()
                        .set_default(15)
                        .set_min(1)
                        .set_step(1, 5)
                        .set_width(char_x * 10),
                    PROTO_PERCENT_FIELD(TargetDef, t, ghost_border_percent));
  ImGui::SameLine();
  ImGui::HelpMarker(
      "If the target gets within the specified percent distance from the wall, it will become a "
      "ghost.");

  ImGui::InputFloat(ImGui::InputFloatParams("NewTargetDelaySeconds")
                        .set_label("New target delay")
                        .set_is_optional()
                        .set_zero_is_unset()
                        .set_step(0.05, 0.25)
                        .set_min(0.01)
                        .set_default(0.2)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(TargetDef, t, new_target_delay_seconds));

  ImGui::InputFloat(ImGui::InputFloatParams("RemoveTargetAfterSeconds")
                        .set_label("Remove after time")
                        .set_is_optional()
                        .set_zero_is_unset()
                        .set_step(0.05, 0.25)
                        .set_min(0.01)
                        .set_default(0.2)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(TargetDef, t, remove_target_after_seconds));

  ImGui::InputFloat(ImGui::InputFloatParams("StaggerInitialTargetsSeconds")
                        .set_label("Initial stagger time")
                        .set_is_optional()
                        .set_zero_is_unset()
                        .set_step(0.05, 0.25)
                        .set_min(0.01)
                        .set_default(0.2)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(TargetDef, t, stagger_initial_targets_seconds));
  ImGui::SameLine();
  ImGui::HelpMarker(Tr("Time in seconds between each target being added at the start of the scenario."));

  bool has_delayed_targets = t->delayed_target_times_size() > 0;
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Delayed targets"));
  ImGui::SameLine();
  ImGui::Checkbox("##DelayTargetsCheckbox", &has_delayed_targets);
  ImGui::SameLine();
  ImGui::HelpMarker(
      "Targets will only be added for the first time after the specified delay. If num_targets=3 "
      "and there are two delays in the list, 1 target will be added immediately, 1 target after "
      "the first time in the list, and 1 target after the second time in the list.");
  if (has_delayed_targets) {
    if (t->delayed_target_times_size() == 0) {
      t->add_delayed_target_times(1);
    }
    int delete_i = -1;
    bool add_item = false;
    ImGui::LoopId loop_id;
    ImGui::Indent();
    for (int i = 0; i < t->delayed_target_times_size(); ++i) {
      auto lid = loop_id.Get("DelayedTargetItem");
      float& value = (*t->mutable_delayed_target_times())[i];

      ImGui::InputFloat(
          ImGui::InputFloatParams("value").set_min(0.01).set_step(0.1, 2).set_width(char_x * 10),
          CreateFloatField(&value));
      ImGui::SameLine();
      if (ImGui::SelectableButton(icons::kClear)) {
        delete_i = i;
      }
    }
    if (ImGui::Button(Tr("Add"))) {
      add_item = true;
    }
    ImGui::Unindent();

    if (delete_i >= 0) {
      t->mutable_delayed_target_times()->erase(t->mutable_delayed_target_times()->begin() +
                                               delete_i);
    }

    if (add_item) {
      t->mutable_delayed_target_times()->Add(1);
    }
  } else {
    t->clear_delayed_target_times();
  }
  ImGui::SpacedSeparator();
}

}  // namespace aim
