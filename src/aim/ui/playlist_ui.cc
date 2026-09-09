#include "playlist_ui.h"

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/resource_name.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/ui/copy_playlist_dialog.h"
#include "aim/ui/editor/scenario_editor_screen.h"
#include "aim/ui/object_browser.h"
#include "aim/ui/playlist_editor_component.h"
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace aim {
namespace {

class AddPlaylistDialog {
 public:
  explicit AddPlaylistDialog(const std::string& id) : id_(id) {}

  void NotifyOpen() {
    open_ = true;
  }

  bool Draw(Application& app) {
    ImGui::IdGuard cid("AddPlaylistDialogContent");
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
        if (ImGui::Button("Add")) {
          auto taken_names =
              app.playlist_manager().GetAllRelativeNamesInBundle(name_.bundle_name());
          *name_.mutable_relative_name() = MakeUniqueName(name_.relative_name(), taken_names);
          app.playlist_manager().UpdatePlaylist(name_.full_name(), PlaylistDef());
          app.playlist_manager().SetCurrentPlaylist(name_.full_name());
          app.history_manager().UpdateRecentView(ObjectType::PLAYLIST, name_.full_name());
          did_add = true;
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
      name_.set(kUserBundleName, "New playlist");
    }
    return did_add;
  }

 private:
  bool open_ = false;
  bool is_open_ = false;

  ResourceName name_;
  std::vector<std::string> bundle_names_;
  std::string id_;
};

class PlaylistComponentImpl : public PlaylistComponent {
 public:
  explicit PlaylistComponentImpl() : app_(GetUiApp()) {}

  void Show(std::shared_ptr<PlaylistRun> run, bool is_playlist_screen) override {
    ImGui::IdGuard cid("PlaylistComponent");

    const std::string& playlist_name = run->playlist.name;

    auto playlist_to_delete = delete_confirmation_dialog_.Draw("Delete");
    if (playlist_to_delete) {
      app_.playlist_manager().DeletePlaylist(playlist_to_delete->name);
      app_.bundle_manager().SaveDirtyBundles();
    }

    if (copy_dialog_.Draw(app_)) {
      if (!is_playlist_screen) {
        app_.state().go_to_app_screen = AppScreen::PLAYLISTS;
      }
    }

    if (playlist_name != current_playlist_name_) {
      current_playlist_name_ = playlist_name;
      ResetForNewCurrentPlaylist();
    }

    if (showing_editor_) {
      if (!editor_component_) {
        std::string base_playlist_name = GetPlaylistNameInfo(playlist_name).base_name;
        editor_component_ = CreatePlaylistEditorComponent(base_playlist_name);
      }
      EditorResult editor_result;
      editor_component_->Draw(&editor_result);
      if (editor_result.editor_closed) {
        editor_component_ = {};
        showing_editor_ = false;
      }
      return;
    }

    std::string updated_playlist_variation_name;
    if (select_variation_dialog_.Draw(&updated_playlist_variation_name)) {
      if (updated_playlist_variation_name != current_playlist_name_) {
        app_.playlist_manager().SetCurrentPlaylist(updated_playlist_variation_name);
        app_.history_manager().UpdateRecentView(ObjectType::PLAYLIST,
                                                updated_playlist_variation_name);
        if (!is_playlist_screen) {
          app_.state().go_to_app_screen = AppScreen::PLAYLISTS;
        }
      }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text(current_playlist_name_);

    const char* menu_id = "CurrentPlaylistMenu";
    if (ImGui::BeginPopupContextItem(menu_id)) {
      bool is_readonly = app_.bundle_manager().IsBundleReadonly(GetBundleName(run->playlist.name));
      if (!is_readonly) {
        if (ImGui::Selectable(std::format("{} Edit", icons::kEdit))) {
          showing_editor_ = true;
        }
      }

      if (ImGui::Selectable(std::format("{} Copy", icons::kContentCopy))) {
        copy_dialog_.NotifyOpen(run->playlist);
      }
      if (ImGui::Selectable(std::format("{} Shuffle", icons::kShuffle))) {
        run->Shuffle(app_.rand());
      }
      if (ImGui::Selectable(std::format("{} Reset run", icons::kRestartAlt))) {
        app_.playlist_manager().ClearRun(run->playlist.name);
        run = app_.playlist_manager().GetCurrentRun();
      }
      if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
        select_variation_dialog_.NotifyOpen(run->playlist.name);
      }

      ImGui::SpacedSeparator();
      if (is_readonly) {
        ImGui::AlignTextToFramePadding();
        ImGui::BeginDisabled();
        ImGui::Text("%s Readonly", icons::kEditOff);
        ImGui::EndDisabled();
        ImGui::HelpTooltip(
            std::format("Bundle \"{}\" is readonly.", GetBundleName(run->playlist.name)));
      } else {
        if (ImGui::Selectable(std::format("{} Delete", icons::kDelete))) {
          delete_confirmation_dialog_.NotifyOpen(std::format("Delete \"{}\"?", playlist_name),
                                                 run->playlist);
        }
      }
      ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::MenuButton()) {
      ImGui::OpenPopup(menu_id);
    }

    // auto highest_complete_level = app_.playlist_manager().GetHighestCompleteLevel(
    //     run->playlist, app_.scenario_manager(), app_.stats_manager());
    //
    // if (highest_complete_level) {
    //   std::string text = std::format("L{}", MaybeIntToString(*highest_complete_level, 2));
    //   ImGui::SameLine();
    //   // ImGui::SetButtonCursorAtRight(text);
    //   ImGui::Button(std::format("L{}", MaybeIntToString(*highest_complete_level, 2)));
    //   ImGui::HelpTooltip("Highest completed level");
    // }

    const PlaylistDef& def = run->playlist.def();

    if (def.levels().base_scenario().size() > 0) {
      auto maybe_base =
          app_.scenario_manager().GetEvaluatedScenarioDef(def.levels().base_scenario());
      if (maybe_base) {
        std::string description = maybe_base->description();
        if (description.size() > 0) {
          ImGui::TextWrapped(description);
        }
      }
    }

    std::string description = def.description();
    if (description.size() > 0) {
      ImGui::TextWrapped(description);
    }

    ImGui::Spacing();
    ImGui::Spacing();
    PlaylistRunComponent("PlaylistRun", run);
  }

 private:
  void ResetForNewCurrentPlaylist() {
    editor_component_ = {};
    showing_editor_ = false;
  }

  bool showing_editor_ = false;
  std::unique_ptr<PlaylistEditorComponent> editor_component_;
  std::string current_playlist_name_;
  NameInfo current_playlist_name_info_;
  SelectVariationDialog select_variation_dialog_ =
      SelectVariationDialog::ForPlaylists("PlaylistVariation");
  Application& app_;

  ImGui::ConfirmationDialog<Playlist> delete_confirmation_dialog_{"DeleteConfirmationDialog1"};
  CopyPlaylistDialog copy_dialog_{"CopyPlaylistDialog"};
};

class PlaylistListComponentImpl : public PlaylistListComponent {
 public:
  explicit PlaylistListComponentImpl() {}

  void Show(PlaylistListResult* result) override {
    if (copy_dialog_.Draw(app_)) {
      app_.bundle_manager().SaveDirtyBundles();
    }
    if (add_dialog_.Draw(app_)) {
      app_.bundle_manager().SaveDirtyBundles();
    }

    ImVec2 char_size = ImGui::CalcTextSize("A");

    ImGui::Spacing();
    if (ImGui::Button(std::format("{} Playlist", icons::kAdd))) {
      add_dialog_.NotifyOpen();
    }

    ImGui::SpacedSeparator();

    ObjectBrowser::Result browser_result;
    browser_->Draw(&browser_result);

    if (browser_result.copy_object_name) {
      auto playlist = app_.playlist_manager().GetPlaylist(*browser_result.copy_object_name);
      if (playlist) {
        copy_dialog_.NotifyOpen(*playlist);
      }
    }

    if (browser_result.selected_object_name) {
      result->open_playlist =
          app_.playlist_manager().GetPlaylist(*browser_result.selected_object_name);
    }
  }

 private:
  Application& app_ = GetUiApp();
  CopyPlaylistDialog copy_dialog_{"CopyPlaylistDialog"};
  AddPlaylistDialog add_dialog_{"AddPlaylistDialog"};
  std::unique_ptr<ObjectBrowser> browser_ = CreateObjectBrowser(ObjectType::PLAYLIST);
};

}  // namespace

void PlaylistRunRightClickMenu(const std::string& scenario_name, PlaylistRun& run) {
  const char* popup_id = "ScenarioItemMenu";
  bool is_levels_playlist = run.playlist.def().has_levels();
  auto& app = GetUiApp();
  if (ImGui::BeginPopupContextItem(popup_id)) {
    if (ImGui::Selectable(std::format("{} Edit", icons::kEdit))) {
      ScenarioEditorOptions opts;
      opts.scenario_name = scenario_name;
      app.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (ImGui::Selectable(std::format("{} Copy", icons::kContentCopy))) {
      ScenarioEditorOptions opts;
      opts.scenario_name = scenario_name;
      opts.is_new_copy = true;
      app.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (ImGui::Selectable(std::format("{} View", icons::kSearch))) {
      app.scenario_manager().SetCurrentScenario(scenario_name);
      app.state().go_to_app_screen = AppScreen::SCENARIOS;
    }
    ImGui::Separator();
    if (!is_levels_playlist) {
      if (ImGui::Selectable("Add copy")) {
        ScenarioEditorOptions opts;
        opts.scenario_name = scenario_name;
        opts.is_new_copy = true;
        opts.add_to_playlist = run.playlist.name;
        opts.force_bundle_name = ResourceName::Parse(opts.add_to_playlist).bundle_name();
        app.PushNextScreen(CreateScenarioEditorScreen(opts));
      }
    }
    if (ImGui::BeginMenu("Add to")) {
      std::string selected_playlist;
      int playlist_count = 0;
      auto recent_playlists = app.history_manager().GetCachedRecentNames(ObjectType::PLAYLIST);
      for (int i = 0; i < recent_playlists->size(); ++i) {
        const std::string& playlist_name = (*recent_playlists)[i];
        auto maybe_playlist = app.playlist_manager().GetPlaylist(playlist_name);
        if (!maybe_playlist.has_value() || maybe_playlist->def().has_levels()) {
          continue;
        }
        if (playlist_count >= 6) {
          break;
        }
        ImGui::IdGuard playlist_id(playlist_name, i);
        playlist_count++;
        if (run.playlist.name != playlist_name && ImGui::MenuItem(playlist_name.c_str())) {
          selected_playlist = playlist_name;
        }
      }
      if (selected_playlist.size() > 0) {
        app.playlist_manager().AddScenarioToPlaylist(selected_playlist, scenario_name);
        app.bundle_manager().SaveDirtyBundles();
      }
      ImGui::EndMenu();
    }
    ImGui::EndPopup();
  }
  ImGui::OpenPopupOnItemClick(popup_id, ImGuiPopupFlags_MouseButtonRight);
}

void PlaylistRunComponent(const std::string& id, std::shared_ptr<PlaylistRun> run) {
  ImGui::IdGuard cid(id);
  if (!ImGui::BeginChild("PlaylistRunComponent")) {
    ImGui::EndChild();
    return;
  }
  auto& app = GetUiApp();
  i64 now_micros = GetNowEpochMicros();
  std::string sample_progress_text = "00/00";
  float progress_width = ImGui::CalcTextSize(sample_progress_text.c_str()).x;

  auto& progress_items = run->progress_list;

  std::vector<float> score_levels;
  std::vector<float> scores;
  std::vector<std::string> scores_help;
  bool has_score_level = false;
  for (const auto& item : progress_items) {
    auto stats = app.stats_manager().GetAggregateStats(item.item.scenario());
    float level = 0;
    if (stats.total_runs > 0) {
      auto scenario_def = app.scenario_manager().GetEvaluatedScenarioDef(item.item.scenario());
      if (scenario_def) {
        level = GetScenarioScoreLevel(stats.high_score_stats.score,
                                      scenario_def->score_targets().target_score());
        if (level > 0) {
          has_score_level = true;
        }
      }
    }
    score_levels.push_back(level);
    scores.push_back(stats.high_score_stats.score);
    scores_help.push_back(GetHowLongAgoStringFromEpochMicros(
        stats.high_score_stats.epoch_seconds * 1000 * 1000, now_micros));
  }

  ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Borders;
  if (!ImGui::BeginTable("PlaylistRuns", has_score_level ? 4 : 3, flags)) {
    return;
  }
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, progress_width);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, progress_width);
  if (has_score_level) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, progress_width);
  }

  for (int i = 0; i < progress_items.size(); ++i) {
    ImGui::TableNextRow();
    ImGui::IdGuard id(i);
    PlaylistItemProgress& progress = run->progress_list[i];
    PlaylistItem& item = progress_items[i].item;
    bool is_selected = i == run->current_index;

    ImGui::TableNextColumn();
    std::string label = item.scenario();
    if (ImGui::Selectable(label.c_str(), is_selected)) {
      run->current_index = i;
      app.state().scenario_run_option = ScenarioRunOption::START_CURRENT;
      app.scenario_manager().SetCurrentScenario(item.scenario());
      app.ReturnToHomeScreen();
    }
    PlaylistRunRightClickMenu(item.scenario(), *run);

    ImGui::TableNextColumn();
    if (scores[i] > 0) {
      ImGui::Text(MaybeIntToString(scores[i], 1));
      ImGui::HelpTooltip(scores_help[i]);
    }

    if (has_score_level) {
      ImGui::TableNextColumn();
      if (score_levels[i] > 0) {
        if (score_levels[i] < 5) {
          ImGui::TextAligned(
              1.0f,
              -FLT_MIN,
              "%s",
              std::format("{}{}", MaybeIntToString(score_levels[i], 1), icons::kBolt).c_str());
        } else {
          ImGui::TextAligned(1.0f, -FLT_MIN, "%s", std::format("5 {}", icons::kVerified).c_str());
        }
      }
    }

    ImGui::TableNextColumn();
    std::string progress_text = std::format("{}/{}", progress.runs_done, item.num_plays());
    ImGui::Text(progress_text);
  }

  ImGui::EndTable();
  ImGui::EndChild();
}

std::unique_ptr<PlaylistComponent> CreatePlaylistComponent() {
  return std::make_unique<PlaylistComponentImpl>();
}

std::unique_ptr<PlaylistListComponent> CreatePlaylistListComponent(UiScreen* screen) {
  return std::make_unique<PlaylistListComponentImpl>();
}

}  // namespace aim
