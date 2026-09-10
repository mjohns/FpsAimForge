#include "object_browser.h"

#include "absl/cleanup/cleanup.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/object_type.h"
#include "aim/common/resource_name.h"
#include "aim/common/search.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/guide_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/labels_manager.h"
#include "aim/core/local_store.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/ui/editor/scenario_editor_screen.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/stats/stats_screen.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"

namespace aim {
namespace {

enum class ViewType : int {
  RECENT = 1,
  ALL = 2,
  STARRED = 3,
};

static std::string GetViewTypeKey(ObjectType type) {
  switch (type) {
    case ObjectType::SCENARIO:
      return "ScenarioViewType";
    case ObjectType::PLAYLIST:
      return "PlaylistViewType";
    case ObjectType::GUIDE:
      return "GuideViewType";
    case ObjectType::THEME:
    case ObjectType::CROSSHAIR:
      break;
  }
  assert(false && "Unsupported object for ViewType");
  return "UnknownViewType";
}

class ObjectBrowserImpl : public ObjectBrowser {
 public:
  explicit ObjectBrowserImpl(ObjectType type) : type_(type) {
    auto maybe_initial_view_type = app_.local_store().GetInt(GetViewTypeKey(type));
    if (maybe_initial_view_type) {
      view_type_ = static_cast<ViewType>(*maybe_initial_view_type);
    }
  }

  // std::function<void(const std::string& name, ObjectBrowserResult*)> draw_item,
  void Draw(Result* result) override {
    ImGui::IdGuard cid(type_name_ + "SearchList");

    auto to_delete = delete_confirmation_dialog_.Draw("Delete");
    if (to_delete) {
      if (type_ == ObjectType::GUIDE) {
        app_.guide_manager().DeleteGuide(*to_delete);
      } else if (type_ == ObjectType::PLAYLIST) {
        app_.playlist_manager().DeletePlaylist(*to_delete);
      } else if (type_ == ObjectType::SCENARIO) {
        app_.scenario_manager().DeleteScenario(*to_delete);
      }
      app_.bundle_manager().SaveDirtyBundles();
    }

    ImVec2 char_size = ImGui::CalcTextSize("A");

    auto recents = app_.history_manager().GetCachedRecentNames(type_);
    if (recents->size() > 0) {
      ImGui::IdGuard cid("QuickAccess");

      // Draw the 10 most recent items.
      ImGui::LoopId loop_id;
      int i = 0;
      for (const std::string& name : *recents) {
        if (!ItemExists(name)) {
          continue;
        }
        i++;
        if (i >= 10) {
          break;
        }
        auto id_guard = loop_id.Get(name);
        DrawItem(name, result);
      }

      ImGui::SpacedSeparator();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", icons::kFilterList);
    ImGui::SameLine();
    if (ImGui::ChipSelector("##ViewType",
                            &view_type_,
                            {
                                {ViewType::ALL, "All"},
                                {ViewType::RECENT, "Recent"},
                                {ViewType::STARRED, "Starred"},
                            })) {
      app_.local_store().PutInt(GetViewTypeKey(type_), (int)view_type_);
    }

    float available_width = ImGui::GetContentRegionAvail().x;
    if (search_text_.size() > 0) {
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      ImGui::SetNextItemWidth(available_width - spacing - ImGui::GetIconButtonWidth(icons::kClear));
    } else {
      ImGui::SetNextItemWidth(available_width);
    }
    ImGui::InputTextWithHint("##SearchInput", icons::kSearch, &search_text_);
    if (search_text_.size() > 0) {
      ImGui::SameLine();
      if (ImGui::ClearButton()) {
        search_text_ = "";
      }
    }

    ImGui::BeginChild("SearchContent");
    auto child_cleanup = absl::MakeCleanup([] { ImGui::EndChild(); });

    auto new_names = GetNames();
    if (new_names == nullptr) {
      return;
    }
    if (new_names != all_names_) {
      all_names_ = new_names;
      UpdateFilteredNames();
    } else if (search_text_ != handled_search_text_) {
      UpdateFilteredNames();
    }

    ImGuiListClipper clipper;
    if (filtered_names_indices_) {
      clipper.Begin(filtered_names_indices_->size());
      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
          int names_i = (*filtered_names_indices_)[i];
          const std::string& name = (*all_names_)[names_i];
          ImGui::IdGuard cid(name, names_i);
          DrawItem(name, result);
        }
      }
    } else {
      clipper.Begin(all_names_->size());
      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
          const std::string& name = (*all_names_)[i];
          ImGui::IdGuard cid(name, i);
          DrawItem(name, result);
        }
      }
    }
  }

 private:
  void DrawItem(const std::string& name, Result* result) {
    // ImGui::IdGuard cid(name);
    if (!ItemExists(name)) {
      DrawMissingItem(name);
      return;
    }

    bool is_scenario = type_ == ObjectType::SCENARIO;
    bool is_playlist = type_ == ObjectType::PLAYLIST;

    if (ImGui::Button(name)) {
      result->selected_object_name = name;
    }

    const char* menu_id = "ItemMenu";
    if (ImGui::BeginPopupContextItem(menu_id)) {
      auto resource_name = ResourceName::Parse(name);
      bool is_readonly = app_.bundle_manager().IsBundleReadonly(resource_name.bundle_name());

      if (is_scenario && !is_readonly) {
        if (ImGui::Selectable(std::format("{} Edit", icons::kEdit))) {
          result->edit_object_name = name;
        }
      }

      if (ImGui::Selectable(std::format("{} Copy", icons::kContentCopy))) {
        result->copy_object_name = name;
      }
      if (type_ != ObjectType::GUIDE) {
        if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
          result->select_variation_object_name = name;
        }
      }

      if (ImGui::Selectable(std::format("{} Recents", icons::kClose))) {
        app_.history_manager().DeleteRecentView(type_, name);
      }

      if (is_scenario) {
        if (ImGui::BeginMenu("Add to")) {
          DrawAddScenarioToPlaylistMenu(name);
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Advanced")) {
          DrawAdvancedScenarioMenu(name, result);
          ImGui::EndMenu();
        }
      }

      if (!is_readonly) {
        ImGui::SpacedSeparator();
        if (ImGui::Selectable(std::format("{} Delete", icons::kDelete))) {
          delete_confirmation_dialog_.NotifyOpen(std::format("Delete \"{}\"?", name), name);
        }
      }
      ImGui::EndPopup();
    }
    ImGui::OpenPopupOnItemClick(menu_id, ImGuiPopupFlags_MouseButtonRight);

    ImGui::SameLine();
    if (app_.labels_manager().IsStarred(type_, name)) {
      if (ImGui::IconButton(icons::kStar)) {
        app_.labels_manager().UnstarItem(type_, name);
      }
    } else {
      if (ImGui::IconButton(icons::kStarOutline)) {
        app_.labels_manager().StarItem(type_, name);
      }
    }
  }

  void DrawAddScenarioToPlaylistMenu(const std::string& name) {
    ImGui::LoopId playlist_loop_id;
    std::string selected_playlist;
    auto recent_playlists = app_.history_manager().GetCachedRecentNames(ObjectType::PLAYLIST);
    int playlist_count = 0;
    for (int i = 0; i < recent_playlists->size(); ++i) {
      if (playlist_count >= 6) {
        break;
      }
      const std::string& playlist_name = (*recent_playlists)[i];
      auto id = playlist_loop_id.Get("AddToPlaylist");
      auto maybe_playlist = app_.playlist_manager().GetPlaylist(playlist_name);
      if (!maybe_playlist.has_value() || maybe_playlist->def().has_levels()) {
        continue;
      }
      playlist_count++;
      if (ImGui::MenuItem(playlist_name.c_str())) {
        selected_playlist = playlist_name;
      }
    }
    if (selected_playlist.size() > 0) {
      app_.playlist_manager().AddScenarioToPlaylist(selected_playlist, name);
      app_.bundle_manager().SaveDirtyBundles();
    }
  }

  void DrawAdvancedScenarioMenu(const std::string& name, Result* result) {
    if (ImGui::Selectable("View stats")) {
      app_.PushNextScreen(
          CreateStatsScreen(name, app_.stats_manager().GetLatestRunId(name), false));
    }
    if (ImGui::Selectable("Copy as reference")) {
      ScenarioEditorOptions opts;
      opts.scenario_name = name;
      opts.is_new_copy = true;
      opts.copy_as_reference = true;
      app_.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (ImGui::Selectable("Create levels playlist")) {
      result->create_level_playlist_for_scenario = name;
    }
  }

  void DrawMissingItem(const std::string& name) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text(name);
    ImGui::SameLine();
    if (view_type_ == ViewType::RECENT) {
      if (ImGui::IconButton(icons::kDelete)) {
        app_.history_manager().DeleteRecentView(type_, name);
      }
      ImGui::HelpTooltip("Delete from recents");
    }
    if (view_type_ == ViewType::STARRED) {
      if (ImGui::IconButton(icons::kStar)) {
        app_.labels_manager().UnstarItem(type_, name);
      }
    }
  }

  bool ItemExists(const std::string& name) {
    switch (type_) {
      case ObjectType::SCENARIO:
        return app_.scenario_manager().GetScenario(name).has_value();
      case ObjectType::PLAYLIST:
        return app_.playlist_manager().GetPlaylist(name).has_value();
      case ObjectType::GUIDE:
        return app_.guide_manager().GetGuide(name).has_value();
      case ObjectType::THEME:
      case ObjectType::CROSSHAIR:
        break;
    }
    assert(false && "Unsupported object type");
    return false;
  }

  std::shared_ptr<std::vector<std::string>> GetNames() {
    if (view_type_ == ViewType::RECENT) {
      return app_.history_manager().GetCachedRecentNames(type_);
    }
    if (view_type_ == ViewType::STARRED) {
      auto items = app_.labels_manager().ListStarredItems(type_);
      return std::shared_ptr<std::vector<std::string>>(items, &items->items);
    }
    switch (type_) {
      case ObjectType::SCENARIO:
        return app_.scenario_manager().scenario_names();
      case ObjectType::PLAYLIST:
        return app_.playlist_manager().playlist_names();
      case ObjectType::GUIDE:
        return app_.guide_manager().guide_names();
      case ObjectType::THEME:
      case ObjectType::CROSSHAIR:
        break;
    }
    assert(false && "Unsupported object type");
    return nullptr;
  }

  void UpdateFilteredNames() {
    handled_search_text_ = search_text_;
    if (search_text_.empty()) {
      // All match. Clear the filter.
      filtered_names_indices_ = {};
      return;
    }

    auto search_words = GetSearchWords(search_text_);
    if (!filtered_names_indices_) {
      filtered_names_indices_ = std::vector<int>{};
    }
    auto& indices = *filtered_names_indices_;
    indices.clear();
    indices.reserve(all_names_->size());

    for (int i = 0; i < all_names_->size(); ++i) {
      if (StringMatchesSearch((*all_names_)[i], search_words)) {
        indices.push_back(i);
      }
    }
  }

  Application& app_ = GetUiApp();
  std::string search_text_;
  std::string handled_search_text_;
  const ObjectType type_;
  const std::string type_name_ = ObjectTypeToString(type_);
  ViewType view_type_ = ViewType::ALL;
  ImGui::ConfirmationDialog<std::string> delete_confirmation_dialog_{"DeleteConfirmationDialog"};
  std::shared_ptr<std::vector<std::string>> all_names_;
  std::optional<std::vector<int>> filtered_names_indices_;
};

}  // namespace

std::unique_ptr<ObjectBrowser> CreateObjectBrowser(ObjectType type) {
  return std::make_unique<ObjectBrowserImpl>(type);
}

}  // namespace aim
