#include "playlist_editor_screen.h"

#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/core/scenario_manager.h"
#include "aim/proto/playlist.pb.h"
#include "aim/ui/base_editor_screen.h"
#include "aim/ui/drag_and_drop.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/select_variation_dialog.h"
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

BaseEditorOptions GetBaseOptions(const PlaylistEditorOptions& opts) {
  BaseEditorOptions base;
  base.name = opts.name;
  base.is_new = opts.is_new_playlist;
  return base;
}

class PlaylistEditorScreen : public BaseEditorScreen {
 public:
  PlaylistEditorScreen(const PlaylistEditorOptions& opts)
      : BaseEditorScreen(ObjectType::PLAYLIST, GetBaseOptions(opts)) {}

 protected:
  void DrawEditor() override {
    auto maybe_description = description_dialog_.Draw();
    if (maybe_description) {
      updated_playlist_.set_description(*maybe_description);
    }
    if (updated_playlist_.description().empty()) {
      updated_playlist_.clear_description();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Type");
    ImGui::SameLine();
    PlaylistType type =
        updated_playlist_.has_levels() ? PlaylistType::LEVELS : PlaylistType::DEFAULT;
    if (ImGui::SimpleTypeDropdown("##TypeSelector", &type, kPlaylistTypes, char_x_ * 10)) {
      if (type == PlaylistType::LEVELS) {
        auto& levels = *updated_playlist_.mutable_levels();
        levels.set_max_level(10);
        levels.set_num_plays_per_level(1);
      }
      if (type == PlaylistType::DEFAULT) {
        updated_playlist_.clear_levels();
      }
    }

    ImGui::SpacedSeparator();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Description");
    ImGui::SameLine();

    if (ImGui::Button(icons::kEdit)) {
      description_dialog_.NotifyOpen(updated_playlist_.description());
    }

    if (updated_playlist_.description().size() > 0) {
      ImGui::Indent();
      ImGui::TextWrapped(updated_playlist_.description());
      ImGui::Unindent();
    }

    ImGui::SpacedSeparator();

    if (type == PlaylistType::LEVELS) {
      DrawLevelsEditor();
    }

    if (type == PlaylistType::DEFAULT) {
      ImGui::BeginChild("PlaylistScrollableContent");
      DrawPlaylistScenariosEditor();
      ImGui::EndChild();
    }
  }

 private:
  void DrawLevelsEditor() {
    auto& levels = *updated_playlist_.mutable_levels();
    if (levels.max_level() < 1) {
      levels.set_max_level(1);
    }

    updated_playlist_.clear_items();

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

  void DrawPlaylistScenariosEditor() {
    updated_playlist_.clear_levels();
    std::string updated_item_name;
    if (select_variation_dialog_.Draw(&updated_item_name)) {
      if (IsValidIndex(*updated_playlist_.mutable_items(), editing_variation_i_)) {
        updated_playlist_.mutable_items(editing_variation_i_)->set_scenario(updated_item_name);
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

    std::vector<PlaylistItem> add_scenarios;
    int add_scenarios_at_i = -1;

    ListUpdater list_updater;
    std::optional<float> last_cm_per_360;
    for (int i = 0; i < updated_playlist_.items_size(); ++i) {
      ImGui::IdGuard lid("PlaylistItem", i);
      ImGui::TableNextRow();
      PlaylistItem& item = *updated_playlist_.mutable_items(i);
      const std::string& scenario_name = item.scenario();

      ImGui::TableNextColumn();
      drag_and_drop_.DrawDragHandle(i, scenario_name);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##ScenarioItemEditor", item.mutable_scenario());

      NameInfo info = GetNameInfo(scenario_name);

      const char* item_menu = "PlaylistItemMenu";
      if (ImGui::BeginPopupContextItem(item_menu)) {
        list_updater.DrawCopyMenuItem(i);
        if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
          editing_variation_i_ = i;
          select_variation_dialog_.NotifyOpen(item.scenario());
        }
        list_updater.DrawMoveMenuItems(i);

        if (info.cm_per_360) {
          if (ImGui::Selectable(std::format("{} Add cm/360 variations", icons::kAdd))) {
            float step = 5;
            if (last_cm_per_360) {
              step = *info.cm_per_360 - *last_cm_per_360;
            }
            add_scenarios_at_i = i;
            NameInfo to_add = info;
            for (int n = 0; n < 5; ++n) {
              *to_add.cm_per_360 += step;
              PlaylistItem add_item = item;
              add_item.set_scenario(to_add.GetFullName());
              add_scenarios.push_back(add_item);
            }
          }
        }
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
      last_cm_per_360 = info.cm_per_360;
    }

    ImGui::EndTable();

    list_updater.Update(updated_playlist_.mutable_items());
    drag_and_drop_.Update(updated_playlist_.mutable_items());
    if (add_scenarios.size() > 0) {
      for (int i = add_scenarios.size() - 1; i >= 0; --i) {
        InsertAtIndex(updated_playlist_.mutable_items(), add_scenarios[i], add_scenarios_at_i + 1);
      }
    }

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
            std::any_of(updated_playlist_.mutable_items()->begin(),
                        updated_playlist_.mutable_items()->end(),
                        [=](const auto& item) { return item.scenario() == scenario_name; });
        return !already_in_playlist;
      };

      std::optional<std::string> selected_scenario =
          SearchSelector(scenario_search_text_, *scenario_names, options);
      if (selected_scenario) {
        PlaylistItem item;
        item.set_scenario(*selected_scenario);
        item.set_num_plays(1);
        *updated_playlist_.add_items() = item;
      }
      ImGui::Unindent();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
  }

  DragAndDrop drag_and_drop_;
  int editing_variation_i_ = -1;
  std::string scenario_search_text_;
  ImGui::MultilineTextEntryDialog description_dialog_{"DescriptionEditor"};
  SelectVariationDialog select_variation_dialog_{"SelectScenarioVariation"};
};

}  // namespace

std::unique_ptr<UiScreen> CreatePlaylistEditorScreen(const PlaylistEditorOptions& options) {
  return std::make_unique<PlaylistEditorScreen>(options);
}

}  // namespace aim
