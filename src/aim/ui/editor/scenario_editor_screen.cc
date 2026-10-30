#include "scenario_editor_screen.h"

#include <absl/cleanup/cleanup.h>
#include <aim/ui/search_selector.h>
#include <aim/ui/select_object_dialog.h>

#include <format>
#include <optional>

#include "absl/strings/ascii.h"
#include "absl/strings/strip.h"
#include "aim/common/field.h"
#include "aim/common/files.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/proto_util.h"
#include "aim/common/resource_name.h"
#include "aim/common/util.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/camera.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/settings_manager.h"
#include "aim/graphics/renderer.h"
#include "aim/scenario/scenario.h"
#include "aim/scenario/scenario_factory.h"
#include "aim/scenario/scenario_overrides.h"
#include "aim/scenario/scenario_util.h"
#include "aim/ui/editor/room_editor.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "aim/ui/editor/scenario_type_editor.h"
#include "aim/ui/editor/target_editor.h"
#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

constexpr const char* kDefaultNewScenarioName = "New Scenario";

class ScenarioEditorScreen : public UiScreen {
 public:
  explicit ScenarioEditorScreen(const ScenarioEditorOptions& opts)
      : UiScreen(),
        target_manager_(GetDefaultSimpleRoom()),
        add_to_playlist_(opts.add_to_playlist) {
    auto themes = app_.settings_manager().ListThemes();
    if (themes.size() > 0) {
      theme_ = app_.settings_manager().GetTheme(themes[0]);
    } else {
      theme_ = GetDefaultTheme();
    }
    settings_ = app_.settings_manager().GetCurrentSettings();
    *def_.mutable_room() = GetDefaultSimpleRoom();
    bundle_names_ = app_.bundle_manager().GetWritableBundleNames();

    if (!opts.scenario_name.empty()) {
      NameInfo original_name_info = GetNameInfo(opts.scenario_name);
      current_level_ = original_name_info.level;
      original_cm_per_360_ = original_name_info.cm_per_360;

      // Initialize scenario def if source is found otherwise show error and exit.
      auto initial_scenario = app_.scenario_manager().GetScenario(opts.scenario_name);
      if (initial_scenario.has_value()) {
        if (opts.copy_as_reference) {
          def_ = {};
          auto* r = def_.mutable_reference_def();
          r->set_scenario_name(opts.scenario_name);
        } else {
          def_ = initial_scenario->unevaluated_def;
        }
      } else {
        notification_popup_.NotifyOpen(
            std::format("Scenario \"{}\" does not exist.", opts.scenario_name));
        exit_after_notification_ = true;
      }
    }

    is_new_scenario_ = opts.is_new_copy;

    std::string default_writable_bundle_name =
        bundle_names_.empty() ? kUserBundleName : bundle_names_[0];
    if (opts.scenario_name.empty()) {
      *name_.mutable_bundle_name() = default_writable_bundle_name;
      *name_.mutable_relative_name() = kDefaultNewScenarioName;
      is_new_scenario_ = true;
    } else {
      // Strip any dynamic suffixes from the name displayed in the editor.
      NameInfo name_info = GetNameInfo(opts.scenario_name);
      name_ = ResourceName::Parse(name_info.base_name);
      if (name_info.level.has_value()) {
        bake_level_ = *name_info.level;
      }

      if (opts.force_bundle_name.size() > 0) {
        *name_.mutable_bundle_name() = opts.force_bundle_name;
      }

      if (app_.bundle_manager().IsBundleReadonly(name_.bundle_name())) {
        // Bundle is not writable. This needs to be a copy within a writable bundle.
        *name_.mutable_bundle_name() = default_writable_bundle_name;
        is_new_scenario_ = true;
      }
    }

    if (is_new_scenario_) {
      MakeRelativeNameUniqueInBundle();
    } else {
      original_name_ = name_;
    }
  }

  void MakeRelativeNameUniqueInBundle() {
    std::string candidate_name = name_.relative_name();
    if (candidate_name != kDefaultNewScenarioName && !candidate_name.ends_with(" Copy")) {
      candidate_name += " Copy";
    }
    auto current_relative_names =
        app_.scenario_manager().GetAllRelativeNamesInBundle(name_.bundle_name());
    std::string final_name = MakeUniqueName(candidate_name, current_relative_names);
    *name_.mutable_relative_name() = final_name;
  }

 protected:
  void DrawNotificationPopup() {
    bool notification_confirmed = notification_popup_.Draw();
    if (notification_confirmed && exit_after_notification_) {
      PopSelf();
    }
  }

  void DrawTopBarWindow() {
    float middle = app_.screen_info().width / 2.0;
    ImGui::SetNextWindowPos(ImVec2(middle, char_x_ / 3.0), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("TopBar",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
      DrawNotificationPopup();
      DrawSaveButtons();
    }

    ImGui::End();
  }

  void DrawSaveButtons() {
    ImVec2 button_size(char_x_ * 9, 0);
    if (ImGui::Button(std::format("{} Play", icons::kPlayArrow), button_size)) {
      PlayScenario();
    }
    ImGui::HelpTooltip(Tr("Try playing the edited version of the scenario."), 0.4);
    ImGui::SameLine();
    std::string save_text = is_new_scenario_ ? std::format("{} Create", icons::kSave)
                                             : std::format("{} Update", icons::kSave);
    if (ImGui::Button(save_text, button_size)) {
      if (SaveScenario()) {
        PopSelf();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Cancel", icons::kClear), button_size)) {
      PopSelf();
    }
  }

  void DrawMainMenuButton() {
    const char* advanced_menu_id = "advanced_menu";
    if (ImGui::BeginPopupContextItem(advanced_menu_id)) {
      if (!is_new_scenario_) {
        if (ImGui::Selectable(Tr("Make new copy"))) {
          is_new_scenario_ = true;
          MakeRelativeNameUniqueInBundle();
        }
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Save the current changes in a new copy of the scenario leaving the original "
            "unchanged.");

        if (ImGui::Selectable(Tr("Copy as reference"))) {
          PopSelf();
          ScenarioEditorOptions opts;
          opts.scenario_name = name_.full_name();
          opts.is_new_copy = true;
          opts.copy_as_reference = true;
          PushNextScreen(CreateScenarioEditorScreen(opts));
        }
        ImGui::SameLine();
        ImGui::HelpMarker("Switch to making a new copy of the current scenario as a reference.");
      }

      if (ImGui::Selectable(Tr("Import Json"))) {
        import_from_json_dialog_.NotifyOpen("");
      }

      if (ImGui::Selectable(Tr("View Json"))) {
        view_json_dialog_.NotifyOpen(MessageToJson(def_));
      }

      ImGui::EndPopup();
    }
    if (ImGui::SelectableButton(icons::kMoreVert)) {
      ImGui::OpenPopup(advanced_menu_id);
    }
  }

  void DrawNameEntryInputs() {
    float width = ImGui::CalcTextSize(name_.bundle_name().c_str()).x;
    ImGui::SimpleDropdown(
        "BundlePicker", name_.mutable_bundle_name(), bundle_names_, width + char_x_ * 3);

    ImGui::SameLine();
    auto available_size = ImGui::GetContentRegionAvail();
    ImGui::SetNextItemWidth(available_size.x);
    ImGui::InputText("##RelativeNameInput", name_.mutable_relative_name());
  }

  void DrawScreen() override {
    ImGui::IdGuard cid("ScenarioEditor");
    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_size_ = char_size;
    char_x_ = char_size_.x;

    if (editing_room_) {
      DrawTopBarWindow();
      DrawRoomEditor();
      return;
    }

    if (app_.BeginFullscreenWindow()) {
      DrawScreenInternal();
    }
    ImGui::End();
  }

  void DrawImportJson() {
    view_json_dialog_.Draw();
    auto imported_json = import_from_json_dialog_.Draw();
    if (imported_json) {
      std::string_view json = absl::StripAsciiWhitespace(*imported_json);
      absl::ConsumeSuffix(&json, ",");

      ScenarioDef imported_def;
      BundleScenario imported_bundle_scenario;
      std::string json_str(json);
      bool imported = false;
      if (JsonToMessage(json_str, &imported_def)) {
        if (!IsDefaultInstance(imported_def)) {
          imported = true;
        }
      }
      // Try to parse it as a bundle scenario.
      if (!imported && JsonToMessage(json_str, &imported_bundle_scenario)) {
        imported = true;
        imported_def = imported_bundle_scenario.def();
        // TODO: Also import the name?
      }

      if (imported && !IsDefaultInstance(imported_def)) {
        std::string def_str = imported_def.DebugString();
        def_ = imported_def;
      } else {
        SetErrorMessage("Failed to parse provided json");
      }
    }
  }

  void DrawScreenInternal() {
    DrawImportJson();

    if (!def_.has_reference_def() && def_.room().type_case() == Room::TYPE_NOT_SET) {
      *def_.mutable_room() = GetDefaultSimpleRoom();
    }

    float padding = char_x_ * 0.3;
    float editor_start_y = ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 1;
    float editor_end_y = app_.screen_info().height - padding;

    auto available_size = ImGui::GetContentRegionAvail();
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersV |
                            ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("MainEditorColumns", 3, flags, ImVec2(0, available_size.y))) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, available_size.x * 0.3f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, available_size.x * 0.35f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextColumn();
      DrawLeftColumn();
      ImGui::TableNextColumn();
      DrawMiddleColumn();
      ImGui::TableNextColumn();
      DrawRightColumn();

      ImGui::EndTable();
    }
  }

  void DrawLeftColumn() {
    ImGui::IdGuard cid("LeftColumn");
    ImGui::Spacing();
    ImGui::Spacing();
    DrawSaveButtons();
    ImGui::SameLine();
    DrawMainMenuButton();

    ImGui::SpacedSeparator();
    DrawNameEntryInputs();
    ImGui::SpacedSeparator();

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Current play level")
                          .set_step(1, 2)
                          .set_is_optional()
                          .set_default(1)
                          .set_width(char_x_ * 8),
                      CreateOptionalFloatField(&current_level_));
    ImGui::SameLine();
    ImGui::HelpMarker("The level to use when playing the scenario from the editor");
    if (current_level_.has_value()) {
      ImGui::SameLine();
      if (ImGui::Button("1")) {
        current_level_ = 1;
      }
      ImGui::SameLine();
      if (ImGui::Button("20")) {
        current_level_ = 20;
      }
      ImGui::SameLine();
      if (ImGui::Button("30")) {
        current_level_ = 30;
      }
      ImGui::SameLine();
      if (ImGui::Button("40")) {
        current_level_ = 40;
      }
    }
    ImGui::SpacedSeparator();

    if (def_.has_reference_def()) {
      ImGui::InputInt(
          ImGui::InputIntParams::WithLabelAsId("Duration")
              .set_is_optional()
              .set_default(60)
              .set_min(5)
              .set_step(5, 5)
              .set_width(char_x_ * 12),
          PROTO_INT_FIELD(ReferenceScenarioDef, def_.mutable_reference_def(), duration_seconds));
    } else {
      float duration_seconds = FirstGreaterThanZero(def_.duration_seconds(), 45);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Duration"));
      ImGui::SameLine();
      ImGui::SetNextItemWidth(char_x_ * 12);
      ImGui::InputFloat("##DurationSeconds", &duration_seconds, 5, 5, "%.0f");
      def_.set_duration_seconds(duration_seconds);
    }

    ImGui::SpacedSeparator();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Scenario type"));
    ImGui::SameLine();

    if (def_.type_case() == ScenarioDef::TYPE_NOT_SET) {
      InitializeScenarioType(def_, ScenarioDef::kStaticDef);
    }
    auto scenario_type = def_.type_case();
    bool is_new_type = ImGui::SimpleTypeDropdown(
        "ScenarioTypeDropdown", &scenario_type, kScenarioTypes, char_x_ * 15);
    if (is_new_type) {
      InitializeScenarioType(def_, scenario_type);
    }

    ImGui::SpacedSeparator();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Shot type"));
    ImGui::SameLine();
    if (def_.has_reference_def()) {
      bool has_shot_type = def_.reference_def().has_shot_type();
      ImGui::Checkbox("##OverrideShotType", &has_shot_type);
      if (has_shot_type) {
        ImGui::Indent();
        DrawShotTypeEditor(*def_.mutable_reference_def()->mutable_shot_type());
        ImGui::Unindent();
      } else {
        def_.mutable_reference_def()->clear_shot_type();
      }
    } else {
      DrawShotTypeEditor(*def_.mutable_shot_type());
    }

    ImGui::SpacedSeparator();

    DrawScoreTargetsEditor(PROTO_PTR_FIELD(ScoreTargets, ScenarioDef, &def_, score_targets));

    ImGui::SpacedSeparator();

    if (ImGui::BeginTabBar("LeftTabBar")) {
      DrawLeftTabItems();
      ImGui::EndTabBar();
    }
  }

  void DrawLevelsTab() {
    if (!BeginMaybeEditedTabItem("Levels", def_.has_level_overrides())) {
      return;
    }
    ImGui::BeginChild("LevelsTab");

    if (def_.has_reference_def()) {
      bool has_level_overrides = def_.has_level_overrides();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Override referenced scenario's levels?");
      ImGui::SameLine();
      ImGui::Checkbox("##LevelOverridesCheck", &has_level_overrides);
      ImGui::SpacedSeparator();
      if (has_level_overrides) {
        ImGui::Indent();
        DrawOverridesEditor("LevelOverrides", def_.mutable_level_overrides(), /*is_levels=*/true);
        ImGui::Unindent();
      } else {
        def_.clear_level_overrides();
      }
    } else {
      DrawOverridesEditor("LevelOverrides", def_.mutable_level_overrides(), /*is_levels=*/true);
      ImGui::SpacedSeparator();
      if (ImGui::Button(Tr("Bake level"))) {
        def_ = ApplyScenarioLevelOverrides(def_, bake_level_);
        bake_level_ = 0;
      }
      ImGui::SameLine();
      ImGui::InputFloat(ImGui::InputFloatParams("BakeLevel").set_step(1, 2).set_width(char_x_ * 8),
                        CreateFloatField(&bake_level_));
      ImGui::SameLine();
      ImGui::HelpMarker(Tr("Fully evaluate the specified level updating values within the scenario being edited."));

      if (IsDefaultInstance(def_.level_overrides())) {
        def_.clear_level_overrides();
      }
    }

    ImGui::EndChild();
    ImGui::EndTabItem();
  }

  void DrawDescriptionTab() {
    bool is_ref = def_.has_reference_def();
    bool has_description =
        is_ref ? def_.reference_def().has_description() : !def_.description().empty();
    if (!BeginMaybeEditedTabItem("Description", has_description)) {
      return;
    }
    ImGui::BeginChild("DescriptionTab");
    if (is_ref) {
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Override referenced scenario's description?");
      ImGui::SameLine();
      bool has_desc = def_.reference_def().has_description();
      ImGui::Checkbox("##OverrideDescCheck", &has_desc);
      ImGui::SpacedSeparator();
      if (has_desc) {
        auto available_size = ImGui::GetContentRegionAvail();
        ImGui::InputTextMultiline("##DescriptionInput",
                                  def_.mutable_reference_def()->mutable_description(),
                                  available_size,
                                  ImGuiInputTextFlags_AllowTabInput);
      } else {
        def_.mutable_reference_def()->clear_description();
      }
    } else {
      auto available_size = ImGui::GetContentRegionAvail();
      ImGui::InputTextMultiline("##DescriptionInput",
                                def_.mutable_description(),
                                available_size,
                                ImGuiInputTextFlags_AllowTabInput);
      if (def_.description().empty()) {
        def_.clear_description();
      }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }

  void DrawLeftTabItems() {
    DrawLevelsTab();
    DrawOverridesTab();
    DrawDescriptionTab();

    if (!def_.has_reference_def()) {
      if (ImGui::TabItemButton("Room")) {
        editing_room_ = true;
      }
    }
  }

  void DrawMiddleColumn() {
    std::string type_str;
    for (const auto& entry : kScenarioTypes) {
      if (entry.first == def_.type_case()) {
        type_str = entry.second;
        break;
      }
    }

    ImGui::IdGuard cid(std::format("MiddleColumn{}", type_str));
    ImGui::Spacing();
    if (ImGui::BeginTabBar("MiddleTabBar")) {
      if (def_.has_reference_def()) {
        if (ImGui::BeginTabItem(Tr("Reference"))) {
          ImGui::BeginChild("ReferenceContent");
          ImGui::Spacing();
          std::string error_message;
          DrawReferenceEditor(def_, &app_, &error_message, &editing_room_);
          if (error_message.size() > 0) {
            SetErrorMessage(error_message);
          }

          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      } else {
        DrawScenarioTypeTabs(def_, &app_);
        DrawInitialTargetLocationTab(def_);
      }
      DrawComparisonTab();
      ImGui::EndTabBar();
    }
  }

  void DrawInitialTargetLocationTab(ScenarioDef& def) {
    std::optional<PtrField<TargetPlacementStrategy>> strat = GetTargetPlacementStrategyField(&def);
    if (!strat) {
      return;
    }

    if (BeginMaybeEditedTabItem("Target location", strat->has())) {
      ImGui::BeginChild("TargetLocationContent");

      bool use_target_placement = strat->has();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Set initial target location"));
      ImGui::SameLine();
      bool changed = ImGui::Checkbox("##UseTargetPlacement", &use_target_placement);
      ImGui::SpacedSeparator();
      if (use_target_placement) {
        DrawTargetPlacementStrategyEditor("Placement", strat->get_mutable());
      } else if (changed) {
        strat->clear();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }

  void DrawTargetTab(ScenarioDef& def) {
    if (ImGui::BeginTabItem("Target")) {
      ImGui::BeginChild("TargetContent");
      ImGui::Spacing();
      DrawTargetEditor(def);

      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }

  void DrawOverridesTab() {
    if (BeginMaybeEditedTabItem("Overrides", !IsDefaultInstance(def_.overrides()))) {
      ImGui::BeginChild("OverridesTab");

      DrawOverridesEditor("Overrides", def_.mutable_overrides());

      if (!def_.has_reference_def()) {
        ImGui::SpacedSeparator();
        if (ImGui::Button(Tr("Bake"))) {
          def_ = ApplyScenarioOverrides(def_);
        }
        ImGui::SameLine();
        ImGui::HelpMarker(Tr("Apply and remove the overrides."));
      }

      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (IsDefaultInstance(def_.overrides())) {
      def_.clear_overrides();
    }
  }

  void DrawRightColumn() {
    ImGui::IdGuard cid("RightColumn");
    ImGui::Spacing();
    if (ImGui::BeginTabBar("RightTabBar")) {
      if (def_.has_reference_def()) {
        DrawOverridesTab();
      } else {
        DrawTargetTab(def_);
        DrawInitialTargetLocationTab(def_);
      }
      DrawComparisonTab();
      ImGui::EndTabBar();
    }
  }

  void DrawComparisonTab() {
    ImGui::IdGuard cid("ComparisonTab");
    if (!ImGui::BeginTabItem(Tr("Compare"))) {
      return;
    }
    ImGui::BeginChild("CompareContainer", ImVec2(0, 0));
    auto cleanup = absl::MakeCleanup([=]() {
      ImGui::EndChild();
      ImGui::EndTabItem();
    });

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Scenario"));
    ImGui::SameLine();
    ScenarioSearchInput(app_, &comparison_scenario_);
    auto maybe_compare_def = app_.scenario_manager().GetEvaluatedScenarioDef(comparison_scenario_);
    if (!maybe_compare_def) {
      if (comparison_scenario_.empty()) {
        ImGui::BeginDisabled();
        ImGui::Text("Enter name of a different scenario to view definition and compare.");
        ImGui::EndDisabled();
      }
      return;
    }

    if (ImGui::BeginTabBar("CompareTabBar")) {
      ScenarioDef compare_def = *maybe_compare_def;
      DrawScenarioTypeTabs(compare_def, &app_);
      DrawInitialTargetLocationTab(compare_def);
      DrawTargetTab(compare_def);
      ImGui::EndTabBar();
    }
  }

  // Returns whether the screen should close
  bool SaveScenario() {
    if (name_.bundle_name().size() == 0 || name_.relative_name().size() == 0) {
      SetErrorMessage("Missing scenario name");
      return false;
    }

    absl::StripAsciiWhitespace(name_.mutable_relative_name());

    NameInfo name_info = GetNameInfo(name_.full_name());
    if (name_info.HasDynamicSuffix()) {
      name_info.base_name = "";
      SetErrorMessage(
          std::format("Unable to save names ending in 'L#' or '#cm'. These are reserved dynamic "
                      "endings. Invalid ending \"{}\"",
                      absl::StripLeadingAsciiWhitespace(name_info.GetFullName())));
      return false;
    }

    auto& mgr = app_.scenario_manager();

    bool is_rename = !is_new_scenario_ && original_name_.has_value() &&
                     original_name_->full_name() != name_.full_name();
    if (is_rename || is_new_scenario_) {
      // Make sure new name is not taken.
      auto existing_scenario_with_name = mgr.GetScenario(name_.full_name());
      if (existing_scenario_with_name.has_value()) {
        SetErrorMessage(std::format("Scenario \"{}\" already exists", name_.full_name()));
        return false;
      }
    }

    if (is_rename) {
      mgr.RenameScenario(original_name_->full_name(), name_.full_name());
    }

    if (add_to_playlist_.size() > 0) {
      app_.playlist_manager().AddScenarioToPlaylist(add_to_playlist_, name_.full_name());
    }

    mgr.UpdateScenario(name_.full_name(), def_);
    if (!app_.bundle_manager().SaveDirtyBundles()) {
      SetErrorMessage("Unable to save bundle to disk.");
      return false;
    }

    {
      // Make sure we preserve the original level/sens in the name for the current scenario.
      NameInfo current_name = GetNameInfo(name_.full_name());
      current_name.level = current_level_;
      current_name.cm_per_360 = original_cm_per_360_;
      app_.scenario_manager().SetCurrentScenario(current_name.GetFullName());
      app_.history_manager().UpdateRecentView(ObjectType::SCENARIO, current_name.GetFullName());
    }

    return true;
  }

  void SetErrorMessage(const std::string& msg) {
    notification_popup_.NotifyOpen(msg);
  }

  void DrawRoomEditor() {
    ImGui::IdGuard cid("RoomEditor");
    ImGui::SetNextWindowBgAlpha(0.4f);
    float width = char_x_ * 25;
    float height = app_.screen_info().height * 0.75;

    ImGui::SetNextWindowPos(ImVec2(char_x_ * 0.3, (app_.screen_info().height - height) / 2.0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    if (ImGui::Begin("Room", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove)) {
      if (ImGui::Button(std::format("{} Back to editor", icons::kArrowBack))) {
        editing_room_ = false;
      }
      ImGui::SameLine();
      SelectObjectDialog::Result import_result;
      if (import_room_dialog_->Draw(&import_result)) {
        if (import_result.selected_objects.size() > 0) {
          auto import_scenario =
              app_.scenario_manager().GetEvaluatedScenarioDef(import_result.selected_objects[0]);
          if (import_scenario) {
            *def_.mutable_room() = import_scenario->room();
          } else {
            SetErrorMessage(
                std::format("Could not read room for \"{}\".", import_result.selected_objects[0]));
          }
        }
      }
      if (ImGui::Button(std::format("{} Import", icons::kDownload))) {
        import_room_dialog_->NotifyOpen();
      }
      ImGui::HelpTooltip("Import room from another scenario.");
      ImGui::SpacedSeparator();
      if (def_.has_reference_def()) {
        DrawRoomEditorInputs(*def_.mutable_reference_def()->mutable_room(), &camera_updates_);
      } else {
        DrawRoomEditorInputs(*def_.mutable_room(), &camera_updates_);
      }
    }
    ImGui::End();
  }

  void Render() override {
    if (!editing_room_) {
      UiScreen::Render();
      return;
    }

    Room room = def_.has_reference_def() ? def_.reference_def().room() : def_.room();

    target_manager_.UpdateRoom(room);
    CameraParams camera_params(room);
    Camera camera(camera_params);

    camera.UpdatePitch(camera.GetPitch() + camera_updates_.delta_pitch);
    camera.UpdateYaw(camera.GetYaw() + camera_updates_.delta_yaw);

    auto look_at = camera.GetLookAt();

    auto projection = GetPerspectiveTransformation(app_.screen_info(), room.horizontal_fov());
    app_.renderer().RenderScenario(projection,
                                   room,
                                   def_.shot_type().type_case(),
                                   theme_,
                                   settings_.health_bar(),
                                   target_manager_.GetTargets(),
                                   look_at);
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    if (user_is_typing) {
      return;
    }
    if (IsMappableKeyDownEvent(event)) {
      std::string event_name = absl::AsciiStrToLower(GetKeyNameForEvent(event));
      bool is_restart = KeyMappingMatchesEvent(
          event_name, app_.settings_manager().GetCurrentSettings().keybinds().restart_scenario());
      bool is_next = KeyMappingMatchesEvent(
          event_name, app_.settings_manager().GetCurrentSettings().keybinds().next_scenario());
      if (is_restart || is_next) {
        PlayScenario();
      }
    }
  }

 private:
  void PlayScenario() {
    CreateScenarioParams params;
    if (def_.has_reference_def()) {
      auto base_scenario =
          app_.scenario_manager().GetEvaluatedScenarioDef(def_.reference_def().scenario_name());
      if (!base_scenario) {
        SetErrorMessage(std::format("Unable to find referenced scenario \"{}\"",
                                    def_.reference_def().scenario_name()));
        return;
      }
      params.def = *base_scenario;
      ApplyReferenceFieldOverrides(def_, &params.def);
    } else {
      params.def = def_;
    }

    params.def = current_level_ ? ApplyScenarioLevelOverrides(params.def, *current_level_)
                                : ApplyScenarioOverrides(params.def);

    params.name = name_.full_name();
    params.force_start_immediately = true;
    params.from_scenario_editor = true;
    PushNextScreen(CreateScenario(params));
  }

  ScenarioDef def_;
  TargetManager target_manager_;
  Theme theme_;
  float char_x_ = 0;
  ImVec2 char_size_{};

  std::vector<std::string> bundle_names_;
  std::optional<ResourceName> original_name_;
  std::optional<float> original_cm_per_360_;
  std::optional<float> current_level_;
  ResourceName name_;
  Settings settings_;

  std::string add_to_playlist_;

  ImGui::NotificationPopup notification_popup_{"Notification"};

  bool editing_room_ = false;

  float bake_level_ = 1;
  bool exit_after_notification_ = false;
  bool is_new_scenario_ = false;
  ImGui::MultilineTextEntryDialog import_from_json_dialog_{"ImportFromJsonDialog"};
  ImGui::MultilineTextEntryDialog view_json_dialog_{"ViewJsonDialog"};
  CameraUpdates camera_updates_;
  std::string comparison_scenario_;
  std::unique_ptr<SelectObjectDialog> import_room_dialog_ =
      CreateSelectObjectDialog("ImportRoomDialog", ObjectType::SCENARIO);
};

}  // namespace

std::unique_ptr<UiScreen> CreateScenarioEditorScreen(const ScenarioEditorOptions& options) {
  return std::make_unique<ScenarioEditorScreen>(options);
}

}  // namespace aim
