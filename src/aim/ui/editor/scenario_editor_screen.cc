#include "scenario_editor_screen.h"

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
#include "aim/ui/editor/room_editor.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "aim/ui/editor/scenario_type_editor.h"
#include "aim/ui/editor/target_editor.h"
#include "aim/ui/search_selector.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

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
      NameInfo original_name_info = GetScenarioNameInfo(opts.scenario_name);
      original_level_ = original_name_info.level;
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
      // Stip any dynamic suffixes from the name displayed in the editor.
      NameInfo name_info = GetScenarioNameInfo(opts.scenario_name);
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
  void DrawTopBar() {
    float middle = app_.screen_info().width / 2.0;
    ImGui::SetNextWindowPos(ImVec2(middle, char_x_ / 3.0), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (!ImGui::Begin("TopBar",
                      nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::End();
      return;
    }

    bool notification_confirmed = notification_popup_.Draw();
    if (notification_confirmed && exit_after_notification_) {
      PopSelf();
    }

    if (ImGui::Button(icons::kPlayArrow)) {
      PlayScenario();
    }
    ImGui::HelpTooltip("Try playing the edited version of the scenario.");

    ImGui::SameLine();
    ImGui::SimpleDropdown("BundlePicker", name_.mutable_bundle_name(), bundle_names_, char_x_ * 11);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x_ * 40);
    ImGui::InputText("##RelativeNameInput", name_.mutable_relative_name());

    ImGui::SameLine();
    std::string save_text = is_new_scenario_ ? std::format("{} Create", icons::kSave)
                                             : std::format("{} Update", icons::kSave);
    if (ImGui::Button(save_text, ImVec2(char_x_ * 8, 0))) {
      if (SaveScenario()) {
        PopSelf();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      PopSelf();
    }

    const char* advanced_menu_id = "advanced_menu";
    if (ImGui::BeginPopupContextItem(advanced_menu_id)) {
      if (!is_new_scenario_) {
        if (ImGui::Selectable("Make new copy")) {
          is_new_scenario_ = true;
          MakeRelativeNameUniqueInBundle();
        }
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Save the current changes in a new copy of the scenario leaving the original "
            "unchanged.");

        if (ImGui::Selectable("Copy as reference")) {
          PopSelf();
          ScenarioEditorOptions opts;
          opts.scenario_name = name_.full_name();
          opts.is_new_copy = true;
          opts.copy_as_reference = true;
          app_.PushNextScreen(CreateScenarioEditorScreen(opts));
        }
        ImGui::SameLine();
        ImGui::HelpMarker("Switch to making a new copy of the current scenario as a reference.");
      }

      if (ImGui::Selectable("Import Json")) {
        import_from_json_dialog_.NotifyOpen("");
      }

      if (ImGui::Selectable("View Json")) {
        view_json_dialog_.NotifyOpen(MessageToJson(def_));
      }

      if (ImGui::Selectable("Compare")) {
        comparison_window_open_ = !comparison_window_open_;
      }
      ImGui::SameLine();
      ImGui::HelpMarker(
          "Open a window displaying values from another scenario. Useful if you want to copy the "
          "strafe patterns from another scenario.");
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::MenuButton()) {
      ImGui::OpenPopup(advanced_menu_id);
    }

    ImGui::End();
  }

  void DrawMainEditor() {
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("MainEditorColumns", 3, flags)) {
      // TODO: make each column default size to 1/3
      ImGui::TableNextColumn();
      ImGui::BeginChild("FirstColumnContainer", ImVec2(0, 0));

      DrawDetailsEditor();
      ImGui::SpacedSeparator();
      DrawSecondaryScenarioTypeEditor(def_);

      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("SecondColumnContainer", ImVec2(0, 0));

      std::string error_message;
      DrawScenarioTypeEditor(
          def_, &app_, &error_message, &editing_room_, &reference_description_dialog_);
      if (error_message.size() > 0) {
        SetErrorMessage(error_message);
      }

      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("ThirdColumnContainer", ImVec2(0, 0));

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Shot type");
      ImGui::SameLine();
      DrawShotTypeEditor(*def_.mutable_shot_type());
      ImGui::SpacedSeparator();

      DrawTargetEditor(def_);
      ImGui::EndChild();

      ImGui::EndTable();
    }
  }

  void DrawScreen() override {
    ImGui::IdGuard cid("ScenarioEditor");
    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_size_ = char_size;
    char_x_ = char_size_.x;

    DrawTopBar();

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

    auto maybe_description = description_dialog_.Draw();
    if (maybe_description) {
      def_.set_description(*maybe_description);
    }

    auto maybe_ref_description = reference_description_dialog_.Draw();
    if (maybe_ref_description) {
      def_.mutable_reference_def()->set_description(*maybe_ref_description);
    }

    if (!def_.has_reference_def() && def_.room().type_case() == Room::TYPE_NOT_SET) {
      *def_.mutable_room() = GetDefaultSimpleRoom();
    }

    if (editing_room_) {
      DrawRoomEditor();
      return;
    }

    float padding = char_x_ * 0.3;
    float editor_start_y = ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 1;
    float editor_end_y = app_.screen_info().height - padding;

    if (comparison_window_open_) {
      if (ImGui::Begin("Compare", &comparison_window_open_)) {
        DrawComparisonWindow();
      }
      ImGui::End();
    }

    if (def_.has_reference_def()) {
      if (BeginMainWindow("ReferenceEditor", 0.6)) {
        ImGui::BeginChild("OnlyColumnContainer", ImVec2(0, 0));
        DrawReferenceEditor();
        ImGui::EndChild();
      }
      ImGui::End();
    } else {
      if (BeginMainWindow("MainEditor", 0.9)) {
        DrawMainEditor();
      }
      ImGui::End();
    }
  }

  void DrawComparisonWindow() {
    ImGui::IdGuard cid("ComparisonWindow");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Scenario");
    ImGui::SameLine();
    ScenarioSearchInput(app_, &comparison_scenario_);
    auto maybe_compare_def = app_.scenario_manager().GetEvaluatedScenarioDef(comparison_scenario_);
    if (!maybe_compare_def) {
      return;
    }

    if (!ImGui::BeginTabBar("CompareTabBar")) {
      return;
    }
    ScenarioDef compare_def = *maybe_compare_def;

    if (ImGui::BeginTabItem("Scenario type")) {
      ImGui::BeginChild("ScenarioTypeContainer", ImVec2(0, 0));
      ImGui::Spacing();
      std::string error_message;
      bool no_op_editing_room = false;
      DrawScenarioTypeEditor(
          compare_def, /*app=*/nullptr, &error_message, &no_op_editing_room, nullptr);
      ImGui::SpacedSeparator();
      DrawSecondaryScenarioTypeEditor(compare_def);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Targets")) {
      ImGui::BeginChild("TargetsContainer", ImVec2(0, 0));
      ImGui::Spacing();
      DrawTargetEditor(compare_def);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Shot type")) {
      ImGui::BeginChild("ShotTypeContainer", ImVec2(0, 0));
      ImGui::Spacing();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Shot type");
      ImGui::SameLine();
      DrawShotTypeEditor(*compare_def.mutable_shot_type());
      ImGui::EndChild();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  void DrawDetailsEditor() {
    float duration_seconds = FirstGreaterThanZero(def_.duration_seconds(), 60);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Duration");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x_ * 12);
    ImGui::InputFloat("##DurationSeconds", &duration_seconds, 5, 5, "%.0f");
    def_.set_duration_seconds(duration_seconds);

    DrawScoreTargetsEditor(PROTO_PTR_FIELD(ScoreTargets, ScenarioDef, &def_, score_targets));

    ImGui::SpacedSeparator();

    ImGui::Text("Level overrides");
    ImGui::SameLine();
    ImGui::HelpMarker("Define how the difficulty of the scenario should be updated per level");
    ImGui::Indent();
    DrawOverridesEditor("LevelOverrides", def_.mutable_level_overrides(), /*is_levels=*/true);
    ImGui::Unindent();

    if (ImGui::Button("Bake level")) {
      def_ = ApplyScenarioLevelOverrides(def_, bake_level_);
      bake_level_ = 0;
    }
    ImGui::SameLine();
    ImGui::InputFloat(ImGui::InputFloatParams("BakeLevel").set_step(1, 2).set_width(char_x_ * 8),
                      CreateFloatField(&bake_level_));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "Fully evaluate the specified level updating values within the scenario being edited.");

    ImGui::SpacedSeparator();

    bool overrides_default_open = !IsDefaultInstance(def_.overrides());
    if (ImGui::TreeNodeEx("Overrides",
                          overrides_default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
      DrawOverridesEditor("Overrides", def_.mutable_overrides());
      if (ImGui::Button("Bake")) {
        def_ = ApplyScenarioOverrides(def_);
      }
      ImGui::SameLine();
      ImGui::HelpMarker("Apply and remove the overrides.");
      ImGui::TreePop();
    }

    ImGui::SpacedSeparator();

    if (ImGui::Button(std::format("{} Room", icons::kEdit))) {
      editing_room_ = true;
    }

    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Description", icons::kEdit))) {
      description_dialog_.NotifyOpen(def_.description());
    }
  }

  // Returns whether the screen should close
  bool SaveScenario() {
    if (name_.bundle_name().size() == 0 || name_.relative_name().size() == 0) {
      SetErrorMessage("Missing scenario name");
      return false;
    }

    absl::StripAsciiWhitespace(name_.mutable_relative_name());

    NameInfo name_info = GetScenarioNameInfo(name_.full_name());
    if (name_info.HasDynamicSuffix()) {
      SetErrorMessage(
          "Unable to save scenario with name ending in 'L#' or '#cm'. These scenarios are "
          "automatically defined.");
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
      NameInfo current_name = GetScenarioNameInfo(name_.full_name());
      current_name.level = original_level_;
      current_name.cm_per_360 = original_cm_per_360_;
      app_.scenario_manager().SetCurrentScenario(current_name.GetFullName());
      app_.history_manager().UpdateRecentView(ObjectType::SCENARIO, current_name.GetFullName());
    }

    return true;
  }

  void SetErrorMessage(const std::string& msg) {
    notification_popup_.NotifyOpen(msg);
  }

  bool BeginMainWindow(const std::string& name, float width_multiple) {
    float padding = char_x_ * 0.3;
    float start_y = ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 1;
    float end_y = app_.screen_info().height - padding;
    float width = app_.screen_info().width * width_multiple;
    float height = end_y - start_y;

    ImGui::SetNextWindowPos(ImVec2((app_.screen_info().width - width) / 2.0, start_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    return ImGui::Begin(
        name.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
  }

  void DrawReferenceEditor() {
    std::string error_message;
    DrawScenarioTypeEditor(
        def_, &app_, &error_message, &editing_room_, &reference_description_dialog_);
    if (error_message.size() > 0) {
      SetErrorMessage(error_message);
    }
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
      ApplyReferenceFieldOverrides(def_, &(*base_scenario));
      params.def = ApplyScenarioOverrides(*base_scenario);
    } else {
      params.def = def_;
    }
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
  std::optional<float> original_level_;
  ResourceName name_;
  Settings settings_;

  std::string add_to_playlist_;

  ImGui::NotificationPopup notification_popup_{"Notification"};

  bool editing_room_ = false;

  bool comparison_window_open_ = false;
  std::string comparison_scenario_;
  float bake_level_ = 1;
  bool exit_after_notification_ = false;
  bool is_new_scenario_ = false;
  ImGui::MultilineTextEntryDialog description_dialog_{"DescriptionDialog"};
  ImGui::MultilineTextEntryDialog reference_description_dialog_{"ReferenceDescriptionDialog"};
  ImGui::MultilineTextEntryDialog import_from_json_dialog_{"ImportFromJsonDialog"};
  ImGui::MultilineTextEntryDialog view_json_dialog_{"ViewJsonDialog"};
  CameraUpdates camera_updates_;
};

}  // namespace

std::unique_ptr<UiScreen> CreateScenarioEditorScreen(const ScenarioEditorOptions& options) {
  return std::make_unique<ScenarioEditorScreen>(options);
}

}  // namespace aim
