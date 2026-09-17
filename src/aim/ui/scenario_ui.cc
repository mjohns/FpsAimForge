#include "scenario_ui.h"

#include <optional>
#include <string>

#include "absl/algorithm/container.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/resource_name.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "aim/ui/editor/scenario_editor_screen.h"
#include "aim/ui/object_browser.h"
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/stats/stats_screen.h"
#include "imgui.h"

namespace aim {
namespace {

class CreateLevelsPlaylistDialog {
 public:
  void NotifyOpen(const std::string& scenario_name) {
    open_ = true;
    scenario_name_ = GetScenarioNameInfo(scenario_name).base_name;
  }

  bool Draw(Application& app) {
    ImGui::IdGuard cid("CreateLevelsPlaylistDialogContent");
    bool did_add = false;
    if (is_open_) {
      if (ImGui::BeginDefaultPopupModal(id_.c_str(), &is_open_)) {
        ImGui::SimpleDropdown("BundlePicker",
                              name_.mutable_bundle_name(),
                              bundle_names_,
                              ImGui::GetFrameHeight() * 9);
        ImGui::SameLine();
        ImGui::InputText("##RelativeNameInput", name_.mutable_relative_name());

        ImGui::Spacing();
        if (ImGui::Button("Create playlist")) {
          auto taken_names =
              app.playlist_manager().GetAllRelativeNamesInBundle(name_.bundle_name());
          *name_.mutable_relative_name() = MakeUniqueName(name_.relative_name(), taken_names);
          PlaylistDef def;
          auto* levels = def.mutable_levels();
          levels->set_base_scenario(scenario_name_);
          levels->set_max_level(30);
          app.playlist_manager().UpdatePlaylist(name_.full_name(), def);
          bool saved = app.bundle_manager().SaveDirtyBundles();
          if (saved) {
            app.playlist_manager().SetCurrentPlaylist(name_.full_name());
            app.history_manager().UpdateRecentView(ObjectType::PLAYLIST, name_.full_name());
            app.state().go_to_app_screen = AppScreen::PLAYLISTS;
            did_add = true;
          }
          // TODO: Error popup if failed to save.
          ImGui::CloseCurrentPopup();
          is_open_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          is_open_ = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }
    if (open_) {
      ImGui::OpenPopup(id_.c_str());
      open_ = false;
      is_open_ = true;
      bundle_names_ = app.bundle_manager().GetWritableBundleNames();

      name_ = ResourceName::Parse(scenario_name_);
      if (app.bundle_manager().IsBundleReadonly(name_.bundle_name())) {
        *name_.mutable_bundle_name() = kUserBundleName;
      }
    }
    return did_add;
  }

 private:
  bool open_ = false;
  bool is_open_ = false;
  std::string scenario_name_;

  ResourceName name_;
  std::vector<std::string> bundle_names_;
  std::string id_ = "CreateLevelsPlaylistDialog";
};

struct ScenarioDialogs {
  ImGui::ConfirmationDialog<std::string> delete_confirmation_dialog{"DeleteConfirmationDialog"};
  CreateLevelsPlaylistDialog create_levels_playlist_dialog;
  SelectVariationDialog select_variation_dialog{"SelectScenarioVariation", ObjectType::SCENARIO};
};

// TODO: Share with ObjectBrowser menu somehow.
void DrawScenarioRightClickMenu(const char* popup_id,
                                const std::string& scenario_name,
                                ScenarioDialogs* dialogs,
                                Application& app) {
  if (!ImGui::BeginPopupContextItem(popup_id)) {
    return;
  }

  bool is_readonly = app.bundle_manager().IsBundleReadonly(GetBundleName(scenario_name));
  if (!is_readonly && ImGui::Selectable("Edit")) {
    ScenarioEditorOptions opts;
    opts.scenario_name = scenario_name;
    app.PushNextScreen(CreateScenarioEditorScreen(opts));
  }
  if (ImGui::Selectable("Copy")) {
    ScenarioEditorOptions opts;
    opts.scenario_name = scenario_name;
    opts.is_new_copy = true;
    app.PushNextScreen(CreateScenarioEditorScreen(opts));
  }
  if (ImGui::Selectable("Select variation")) {
    dialogs->select_variation_dialog.NotifyOpen(scenario_name);
  }
  if (ImGui::Selectable("Remove from recents")) {
    app.history_manager().DeleteRecentView(ObjectType::SCENARIO, scenario_name);
  }
  if (ImGui::BeginMenu("Add to")) {
    ImGui::LoopId playlist_loop_id;
    std::string selected_playlist;
    auto recent_playlists = app.history_manager().GetCachedRecentNames(ObjectType::PLAYLIST);
    int playlist_count = 0;
    for (int i = 0; i < recent_playlists->size(); ++i) {
      if (playlist_count >= 6) {
        break;
      }
      const std::string& playlist_name = (*recent_playlists)[i];
      auto id = playlist_loop_id.Get("AddToPlaylist");
      auto maybe_playlist = app.playlist_manager().GetPlaylist(playlist_name);
      if (!maybe_playlist.has_value() || maybe_playlist->def().has_levels()) {
        continue;
      }
      playlist_count++;
      if (ImGui::MenuItem(playlist_name.c_str())) {
        selected_playlist = playlist_name;
      }
    }
    if (selected_playlist.size() > 0) {
      app.playlist_manager().AddScenarioToPlaylist(selected_playlist, scenario_name);
      app.bundle_manager().SaveBundle(ResourceName::Parse(selected_playlist).bundle_name());
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Advanced")) {
    if (ImGui::Selectable("View stats")) {
      app.PushNextScreen(CreateStatsScreen(
          scenario_name, app.stats_manager().GetLatestRunId(scenario_name), false));
    }
    if (ImGui::Selectable("Copy as reference")) {
      ScenarioEditorOptions opts;
      opts.scenario_name = scenario_name;
      opts.is_new_copy = true;
      opts.copy_as_reference = true;
      app.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (ImGui::Selectable("Create levels playlist")) {
      dialogs->create_levels_playlist_dialog.NotifyOpen(scenario_name);
    }
    ImGui::EndMenu();
  }
  if (!is_readonly) {
    ImGui::SpacedSeparator();
    if (ImGui::Selectable("Delete")) {
      std::string base_name = GetScenarioNameInfo(scenario_name).base_name;
      dialogs->delete_confirmation_dialog.NotifyOpen(std::format("Delete \"{}\"?", base_name),
                                                     base_name);
    }
  }
  ImGui::EndPopup();
}

class ScenariosComponentImpl : public ScenariosComponent {
 public:
  void Show() override {
    std::optional<std::string> scenario_to_delete =
        dialogs_.delete_confirmation_dialog.Draw("Delete");
    if (scenario_to_delete) {
      auto maybe_scenario = app_.scenario_manager().GetScenario(*scenario_to_delete);
      if (maybe_scenario.has_value()) {
        app_.scenario_manager().DeleteScenario(maybe_scenario->name);
        app_.bundle_manager().SaveDirtyBundles();
      }
    }
    dialogs_.create_levels_playlist_dialog.Draw(app_);

    std::string updated_scenario_variation_name;
    if (dialogs_.select_variation_dialog.Draw(&updated_scenario_variation_name)) {
      app_.scenario_manager().SetCurrentScenario(updated_scenario_variation_name);
      // TODO: Add to history?
    }

    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("ScenarioColumns", 2, flags)) {
      ImGui::TableNextColumn();
      DrawScenarioBrowserPanel();

      ImGui::TableNextColumn();
      DrawCurrentScenarioPanel();

      ImGui::EndTable();
    }
  }

 private:
  void DrawScenarioBrowserPanel() {
    ImGui::IdGuard cid("ScenarioBrowser");
    ImGui::Spacing();
    if (ImGui::Button(std::format("{} Scenario", icons::kAdd))) {
      ScenarioEditorOptions opts;
      opts.scenario_name = "";
      opts.is_new_copy = true;
      app_.PushNextScreen(CreateScenarioEditorScreen(opts));
    }

    ImGui::SpacedSeparator();

    ObjectBrowser::Result result;
    browser_->Draw(&result);
    if (result.selected_object_name) {
      app_.scenario_manager().SetCurrentScenario(*result.selected_object_name);
    }
    if (result.edit_object_name) {
      ScenarioEditorOptions opts;
      opts.scenario_name = *result.edit_object_name;
      app_.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (result.copy_object_name) {
      ScenarioEditorOptions opts;
      opts.scenario_name = *result.copy_object_name;
      opts.is_new_copy = true;
      app_.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (result.select_variation_object_name) {
      dialogs_.select_variation_dialog.NotifyOpen(*result.select_variation_object_name);
    }
    if (result.create_level_playlist_for_scenario) {
      dialogs_.create_levels_playlist_dialog.NotifyOpen(*result.create_level_playlist_for_scenario);
    }
  }

  void DrawCurrentScenarioPanel() {
    ImGui::IdGuard cid("CurrentScenario");
    auto maybe_current_scenario = app_.scenario_manager().GetCurrentScenario();
    if (!maybe_current_scenario) {
      return;
    }
    const ScenarioItem& item = *maybe_current_scenario;
    std::optional<ScenarioDef> evaluated_def =
        app_.scenario_manager().GetEvaluatedScenarioDef(item.name);
    ImGui::Spacing();
    if (ImGui::Button(std::format("{}", icons::kPlayArrow))) {
      app_.state().scenario_run_option = ScenarioRunOption::START_CURRENT;
    }
    ImGui::SameLine();
    ImGui::Text(item.name);

    ImGui::SameLine();
    const char* popup_id = "CurrentScenarioMenu";
    DrawScenarioRightClickMenu(popup_id, item.name, &dialogs_, app_);
    ImGui::OpenPopupOnItemClick(popup_id, ImGuiPopupFlags_MouseButtonRight);
    if (ImGui::MenuButton()) {
      ImGui::OpenPopup(popup_id);
    }

    if (evaluated_def) {
      std::vector<std::string> chips;
      auto type_it = kScenarioTypeDisplayNameMap.find(evaluated_def->type_case());
      if (type_it != kScenarioTypeDisplayNameMap.end()) {
        chips.push_back(type_it->second);
      }

      auto shot_type_it = kShotTypeDisplayNameMap.find(evaluated_def->shot_type().type_case());
      if (shot_type_it != kShotTypeDisplayNameMap.end()) {
        chips.push_back(shot_type_it->second);
      }

      if (chips.size() > 0) {
        ImGui::SpacedSeparator();
        // TODO: Use better visual type other than disabled button.
        ImGui::BeginDisabled();
        ImGui::LoopId lid;
        for (int i = 0; i < chips.size(); ++i) {
          auto id = lid.Get();
          if (i > 0) {
            ImGui::SameLine();
          }
          ImGui::Button(chips[i]);
        }
        ImGui::EndDisabled();
      }
    }

    auto stats = app_.stats_manager().GetAggregateStats(item.name);
    if (stats.total_runs > 0) {
      ImGui::IdGuard cid("HighScore");
      ImGui::SpacedSeparator();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("High score");
      ImGui::SameLine();
      if (ImGui::Button(MaybeIntToString(stats.high_score_stats.score))) {
        app_.GetCurrentScreen()->PushNextScreen(
            CreateStatsScreen(item.name, stats.high_score_stats.stats_id, false));
      }
      ImGui::HelpTooltip("View stats for run.");
      std::string high_score_time = GetHowLongAgoStringFromEpochSeconds(
          stats.high_score_stats.epoch_seconds, GetNowEpochSeconds());
      ImGui::SameLine();
      ImGui::TextFmt("({})", high_score_time);

      std::string last_run_str = GetHowLongAgoStringFromEpochSeconds(
          stats.last_run_stats.epoch_seconds, GetNowEpochSeconds());
      if (stats.total_runs == 1) {
        ImGui::TextFmt("1 run ({})", last_run_str);
      } else {
        ImGui::TextFmt("{} runs ({})", stats.total_runs, last_run_str);
      }
    }

    std::string description = FirstNonEmpty(item.unevaluated_def.description(),
                                            item.unevaluated_def.reference_def().description());
    if (description.size() > 0) {
      ImGui::SpacedSeparator();
      ImGui::TextWrapped(description);
    }

    if (cached_scenario_name_ != item.name) {
      // Calculate "expensive" things only the first time the scenario is switched to.
      matching_playlists_ = app_.playlist_manager().FindPlaylistsContainingScenario(item.name);
      absl::c_sort(*matching_playlists_);
      referencing_scenarios_ = app_.scenario_manager().GetReferencingScenarios(item.name);
      absl::c_sort(referencing_scenarios_);
      cached_scenario_name_ = item.name;
    }

    std::string referenced_scenario = item.unevaluated_def.reference_def().scenario_name();
    if (!referenced_scenario.empty()) {
      ImGui::SpacedSeparator();
      ImGui::IdGuard cid("ReferencedScenario");
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Referenced scenario");
      ImGui::SameLine();
      ImGui::HelpMarker(
          "This scenario references/extends the following scenario. Editing the referenced "
          "scenario will alter the behavior of this scenario.");
      ImGui::SameLine();
      if (ImGui::Button(referenced_scenario)) {
        app_.scenario_manager().SetCurrentScenario(referenced_scenario);
      }
      const char* popup_id = "ReferencedScenarioMenu";
      DrawScenarioRightClickMenu(popup_id, referenced_scenario, &dialogs_, app_);
      ImGui::OpenPopupOnItemClick(popup_id, ImGuiPopupFlags_MouseButtonRight);
    }

    if (!referencing_scenarios_.empty()) {
      ImGui::SpacedSeparator();
      ImGui::Text("Referencing scenarios");
      ImGui::SameLine();
      ImGui::HelpMarker(
          "Scenarios that extend this scenario. Changing this scenario would change the following "
          "ones too.");
      ImGui::Indent();
      ImGui::LoopId loop_id;
      for (const std::string& name : referencing_scenarios_) {
        auto lid = loop_id.Get();
        if (ImGui::Button(name)) {
          app_.scenario_manager().SetCurrentScenario(name);
        }
        const char* popup_id = "ReferencingScenarioMenu";
        DrawScenarioRightClickMenu(popup_id, name, &dialogs_, app_);
        ImGui::OpenPopupOnItemClick(popup_id, ImGuiPopupFlags_MouseButtonRight);
      }
      ImGui::Unindent();
    }

    if (!matching_playlists_->empty()) {
      ImGui::SpacedSeparator();
      ImGui::Text("Related playlists");
      ImGui::Indent();
      for (const std::string& playlist_name : *matching_playlists_) {
        if (ImGui::Button(playlist_name)) {
          app_.playlist_manager().SetCurrentPlaylist(playlist_name);
          app_.state().go_to_app_screen = AppScreen::PLAYLISTS;
        }
      }
      ImGui::Unindent();
    }
  }

  Application& app_ = GetUiApp();
  std::unique_ptr<ObjectBrowser> browser_ = CreateObjectBrowser(ObjectType::SCENARIO);

  // The scenario which cached information was stored for
  std::string cached_scenario_name_;
  std::optional<std::vector<std::string>> matching_playlists_;
  std::vector<std::string> referencing_scenarios_;

  ScenarioDialogs dialogs_;
};

}  // namespace

std::unique_ptr<ScenariosComponent> CreateScenariosComponent() {
  return std::make_unique<ScenariosComponentImpl>();
}

}  // namespace aim
