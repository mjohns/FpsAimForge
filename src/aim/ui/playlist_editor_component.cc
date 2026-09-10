#include "playlist_editor_component.h"

#include <algorithm>

#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/proto_util.h"
#include "aim/common/resource_name.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/ui/drag_and_drop.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"

namespace aim {
namespace {

enum class PlaylistType {
  DEFAULT,
  LEVELS,
};

const std::vector<std::pair<PlaylistType, std::string>> kPlaylistTypes{
    {PlaylistType::DEFAULT, "Default"},
    {PlaylistType::LEVELS, "Levels"},
};

class PlaylistEditorComponentImpl : public PlaylistEditorComponent {
 public:
  explicit PlaylistEditorComponentImpl(const std::string& playlist_name) : app_(GetUiApp()) {
    std::shared_ptr<PlaylistRun> run = app_.playlist_manager().GetRun(playlist_name);
    if (run != nullptr) {
      ResourceName name = ResourceName::Parse(run->playlist.name);
      new_playlist_name_ = name.relative_name();
      original_playlist_name_ = run->playlist.name;
      bundle_name_ = name.bundle_name();
      auto maybe_playlist = app_.playlist_manager().GetPlaylist(run->playlist.name);
      if (maybe_playlist) {
        original_playlist_def_ = maybe_playlist->def();
        description_ = original_playlist_def_.description();
        for (auto& i : maybe_playlist->items()) {
          scenario_items_.push_back(i);
        }
      }
      if (maybe_playlist->def().has_levels()) {
        levels_ = maybe_playlist->def().levels();
      }
    }
  }

  void Draw(EditorResult* result) {
    ImGui::IdGuard cid("PlaylistEditor");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    notification_popup_.Draw();
    auto maybe_description = description_dialog_.Draw();
    if (maybe_description) {
      description_ = *maybe_description;
    }

    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_x_ = char_size.x;

    ImGui::Spacing();

    if (ImGui::Button("Save")) {
      if (SavePlaylist()) {
        result->editor_closed = true;
        result->playlist_updated = true;
        return;
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      result->editor_closed = true;
      return;
    }

    ImGui::SpacedSeparator();

    ImGui::AlignTextToFramePadding();
    ImGui::Text(bundle_name_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(400);
    ImGui::InputText("###PlaylistNameInput", &new_playlist_name_);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Type");
    ImGui::SameLine();
    PlaylistType type = levels_.has_value() ? PlaylistType::LEVELS : PlaylistType::DEFAULT;
    if (ImGui::SimpleTypeDropdown("##TypeSelector", &type, kPlaylistTypes, char_x_ * 10)) {
      if (type == PlaylistType::LEVELS) {
        levels_ = LevelsPlaylistDef();
        auto& levels = *levels_;
        levels.set_max_level(10);
        levels.set_num_plays_per_level(1);
      }
      if (type == PlaylistType::DEFAULT) {
        levels_ = {};
      }
    }

    ImGui::SpacedSeparator();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Description");
    ImGui::SameLine();

    if (ImGui::Button(icons::kEdit)) {
      description_dialog_.NotifyOpen(description_);
    }

    if (description_.size() > 0) {
      ImGui::Indent();
      ImGui::TextWrapped(description_);
      ImGui::Unindent();
    }

    ImGui::SpacedSeparator();

    if (type == PlaylistType::LEVELS) {
      DrawLevelsEditor();
    }

    if (type == PlaylistType::DEFAULT) {
      ImGui::BeginChild("PlaylistScrollableContent");
      DrawPlaylistScenariosEditor(result);
      ImGui::EndChild();
    }
  }

 private:
  void DrawLevelsEditor() {
    if (!levels_) {
      levels_ = LevelsPlaylistDef();
    }

    auto& levels = *levels_;
    if (levels.max_level() < 1) {
      levels.set_max_level(1);
    }

    scenario_items_.clear();

    ImGui::IdGuard cid("LevelsEditor");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Base scenario");
    ImGui::SameLine();
    ImGui::InputText("###BaseScenarioInput", levels.mutable_base_scenario());
    if (levels.base_scenario().size() > 0) {
      ImGui::Indent();
      auto scenario_names = app_.scenario_manager().scenario_names();
      std::optional<std::string> selected_scenario =
          SearchSelector(levels.base_scenario(), *scenario_names);
      if (selected_scenario) {
        // set base_name
        levels.set_base_scenario(*selected_scenario);
      }
      ImGui::Unindent();
    }

    ImGui::Separator();

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Max level")
                          .set_step(1, 2)
                          .set_min(2)
                          .set_default(10)
                          .set_width(char_x_ * 10),
                      PROTO_FLOAT_FIELD(LevelsPlaylistDef, &levels, max_level));
    ImGui::InputInt(ImGui::InputIntParams::WithLabelAsId("Plays per level")
                        .set_step(1, 2)
                        .set_min(1)
                        .set_default(1)
                        .set_width(char_x_ * 10),
                    PROTO_INT_FIELD(LevelsPlaylistDef, &levels, num_plays_per_level));

    ImGui::Separator();

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Min level")
                          .set_is_optional()
                          .set_step(1, 2)
                          .set_default(1)
                          .set_width(char_x_ * 10),
                      PROTO_FLOAT_FIELD(LevelsPlaylistDef, &levels, min_level));
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Step")
                          .set_step(0.5, 2)
                          .set_default(1)
                          .set_is_optional()
                          .set_width(char_x_ * 10),
                      PROTO_FLOAT_FIELD(LevelsPlaylistDef, &levels, level_step));
  }

  void DrawPlaylistScenariosEditor(EditorResult* result) {
    std::string updated_item_name;
    if (select_variation_dialog_.Draw(&updated_item_name)) {
      if (IsValidIndex(scenario_items_, editing_variation_i_)) {
        scenario_items_[editing_variation_i_].set_scenario(updated_item_name);
      }
    }

    if (!ImGui::BeginTable("Playlists", 4, ImGui::kDefaultTableFlags)) {
      return;
    }

    float drag_width = drag_and_drop_.GetDragWidth();
    float count_width = char_x_ * 8;
    float menu_width = ImGui::GetIconButtonWidth(icons::kMoreVert);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, drag_width);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, count_width);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, menu_width);

    ListUpdater list_updater;
    for (int i = 0; i < scenario_items_.size(); ++i) {
      ImGui::IdGuard lid("PlaylistItem", i);
      ImGui::TableNextRow();
      PlaylistItem& item = scenario_items_[i];
      const std::string& scenario_name = item.scenario();

      ImGui::TableNextColumn();
      drag_and_drop_.DrawDragHandle(i, scenario_name);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##ScenarioItemEditor", item.mutable_scenario());

      const char* item_menu = "PlaylistItemMenu";
      if (ImGui::BeginPopupContextItem(item_menu)) {
        list_updater.DrawCopyMenuItem(i);
        if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
          editing_variation_i_ = i;
          select_variation_dialog_.NotifyOpen(item.scenario());
        }
        list_updater.DrawMoveMenuItems(i);
        ImGui::SpacedSeparator();
        list_updater.DrawDeleteMenuItem(i);
        ImGui::EndPopup();
      }
      ImGui::OpenPopupOnItemClick(item_menu, ImGuiPopupFlags_MouseButtonRight);

      ImGui::TableNextColumn();

      u32 num_plays = item.num_plays();
      u32 step = 1;
      ImGui::SetNextItemWidth(count_width);
      ImGui::InputScalar("###NumPlays", ImGuiDataType_U32, &num_plays, &step, nullptr, "%u");

      ImGui::TableNextColumn();
      if (ImGui::IconButton(icons::kMoreVert)) {
        ImGui::OpenPopup(item_menu);
      }

      item.set_num_plays(num_plays);
    }

    ImGui::EndTable();

    list_updater.UpdateVector(&scenario_items_);
    drag_and_drop_.UpdateVector(&scenario_items_);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text("Add scenario");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x_ * 18);
    ImGui::InputText("###AddScenarioInput", &scenario_search_text_);
    ImGui::SameLine();
    if (ImGui::ClearButton()) {
      scenario_search_text_ = "";
    }
    if (scenario_search_text_.size() > 0) {
      ImGui::Indent();
      auto scenario_names = app_.scenario_manager().scenario_names();
      SearchSelectorOptions options;
      options.additional_predicate = [&](const std::string& scenario_name) {
        bool already_in_playlist =
            std::any_of(scenario_items_.begin(), scenario_items_.end(), [=](const auto& item) {
              return item.scenario() == scenario_name;
            });
        return !already_in_playlist;
      };

      std::optional<std::string> selected_scenario =
          SearchSelector(scenario_search_text_, *scenario_names, options);
      if (selected_scenario) {
        PlaylistItem item;
        item.set_scenario(*selected_scenario);
        item.set_num_plays(1);
        scenario_items_.push_back(item);
      }
      ImGui::Unindent();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
  }

  bool SavePlaylist() {
    PlaylistDef playlist;
    playlist.set_description(description_);

    if (levels_) {
      *playlist.mutable_levels() = *levels_;
    } else {
      playlist.mutable_items()->Add(scenario_items_.begin(), scenario_items_.end());
    }

    ResourceName final_name(bundle_name_, new_playlist_name_);
    NameInfo final_name_info = GetPlaylistNameInfo(final_name.full_name());
    if (final_name_info.HasDynamicSuffix()) {
      notification_popup_.NotifyOpen("Cannot name playlist with explicit cm/360 suffix.");
      return false;
    }

    bool name_changed = final_name.full_name() != original_playlist_name_;
    if (name_changed) {
      // Need to move file.
      std::vector<std::string> taken_names =
          app_.playlist_manager().GetAllRelativeNamesInBundle(bundle_name_);
      final_name.set(bundle_name_, MakeUniqueName(new_playlist_name_, taken_names));
      if (!app_.playlist_manager().RenamePlaylist(original_playlist_name_,
                                                  final_name.full_name())) {
        notification_popup_.NotifyOpen(
            std::format("Playlist with name \"{}\" already exists", final_name.full_name()));
        return false;
      }
      app_.history_manager().UpdateRecentView(ObjectType::PLAYLIST, final_name.full_name());
      std::shared_ptr<PlaylistRun> current_run = app_.playlist_manager().GetCurrentRun();
      if (current_run != nullptr && current_run->playlist.name == original_playlist_name_) {
        app_.playlist_manager().SetCurrentPlaylist(final_name.full_name());
      }
    }

    app_.playlist_manager().UpdatePlaylist(final_name.full_name(), playlist);
    bool saved = app_.bundle_manager().SaveDirtyBundles();
    if (!saved) {
      notification_popup_.NotifyOpen("Failed to save playlist to disk");
    }
    return saved;
  }

  Application& app_;
  std::vector<PlaylistItem> scenario_items_;
  DragAndDrop drag_and_drop_;
  int editing_variation_i_ = -1;

  std::string original_playlist_name_;
  std::string bundle_name_;
  std::string source_base_scenario_;

  std::optional<LevelsPlaylistDef> levels_;
  std::string scenario_search_text_;
  std::string new_playlist_name_;
  float char_x_ = 0;
  PlaylistDef original_playlist_def_;
  ImGui::NotificationPopup notification_popup_{"Notification"};
  std::string description_;
  ImGui::MultilineTextEntryDialog description_dialog_{"DescriptionEditor"};
  SelectVariationDialog select_variation_dialog_ =
      SelectVariationDialog::ForScenarios("SelectScenarioVariation");
};

}  // namespace

std::unique_ptr<PlaylistEditorComponent> CreatePlaylistEditorComponent(
    const std::string& playlist_name) {
  return std::make_unique<PlaylistEditorComponentImpl>(playlist_name);
}

}  // namespace aim
