#include "object_browser.h"

#include "absl/cleanup/cleanup.h"
#include "absl/strings/ascii.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
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
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/stats/stats_screen.h"
#include "aim/ui/ui_app.h"
#include "editor/scenario_editor_common.h"
#include "imgui.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

enum TargetScoreFilter { NO_FILTER, HAS_TARGET_SCORE, MISSING_TARGET_SCORE };

inline const std::vector<std::pair<TargetScoreFilter, std::string>> kTargetScoreFilters{
    {TargetScoreFilter::NO_FILTER, ""},
    {TargetScoreFilter::HAS_TARGET_SCORE, "Has target score"},
    {TargetScoreFilter::MISSING_TARGET_SCORE, "No target score"},
};

struct AllNames {
  std::shared_ptr<std::vector<std::string>> names1 = std::make_shared<std::vector<std::string>>();
  std::shared_ptr<std::vector<std::string>> names2 = std::make_shared<std::vector<std::string>>();
  std::shared_ptr<std::vector<std::string>> names3 = std::make_shared<std::vector<std::string>>();

  size_t size() {
    return names1->size() + names2->size() + names3->size();
  }

  // names2/3 are always dynamic readonly items
  bool IsReadonly(int i) {
    return i >= names1->size();
  }

  const std::string& get(int i) const {
    if (i < names1->size()) {
      return (*names1)[i];
    }
    i -= names1->size();
    if (i < names2->size()) {
      return (*names2)[i];
    }
    i -= names2->size();
    return (*names3)[i];
  }

  bool operator==(const AllNames& other) const {
    return names1.get() == other.names1.get() && names2.get() == other.names2.get() &&
           names3.get() == other.names3.get();
  }
};

enum class ViewType : int {
  RECENT = 1,
  ALL = 2,
  STARRED = 3,
  // Excludes dynamic items like Levels/cm360.
  BASE = 4,
};

static std::string GetViewTypeKey(ObjectType type, bool is_selector) {
  std::string prefix = is_selector ? "Selector" : "";
  switch (type) {
    case ObjectType::SCENARIO:
      return prefix + "ScenarioViewType";
    case ObjectType::PLAYLIST:
      return prefix + "PlaylistViewType";
    case ObjectType::GUIDE:
      return prefix + "GuideViewType";
    case ObjectType::THEME:
    case ObjectType::CROSSHAIR:
      break;
  }
  assert(false && "Unsupported object for ViewType");
  return prefix + "UnknownViewType";
}

class ObjectBrowserImpl : public ObjectBrowser {
 public:
  explicit ObjectBrowserImpl(ObjectType type, bool is_selector)
      : is_selector_(is_selector), type_(type), bundle_names_(SecondsToMicros(0.6)) {
    bundle_names_.SetLoader([&]() { return app_.bundle_manager().GetBundleNames(); });
    auto maybe_initial_view_type = app_.local_store().GetInt(GetViewTypeKey(type, is_selector_));
    if (maybe_initial_view_type) {
      view_type_ = static_cast<ViewType>(*maybe_initial_view_type);
    }

    shot_types_.insert(shot_types_.end(), kShotTypes.begin(), kShotTypes.end());
  }

  // std::function<void(const std::string& name, ObjectBrowserResult*)> draw_item,
  void Draw(Result* result) override {
    ImGui::IdGuard cid(type_name_ + "SearchList");

    std::string selected_variation_name;
    if (select_variation_dialog_.Draw(&selected_variation_name)) {
      result->selected_object_name = selected_variation_name;
    }

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

    // auto recents = app_.history_manager().GetCachedRecentNames(type_);
    // if (recents->size() > 0) {
    //   ImGui::IdGuard cid("QuickAccess");
    //
    //   // Draw the 10 most recent items.
    //   ImGui::LoopId loop_id;
    //   int i = 0;
    //   for (const std::string& name : *recents) {
    //     if (!ItemExists(name)) {
    //       continue;
    //     }
    //     i++;
    //     if (i >= 10) {
    //       break;
    //     }
    //     auto id_guard = loop_id.Get(name);
    //     DrawItem(name, result);
    //   }
    //
    //   ImGui::SpacedSeparator();
    // }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", icons::kFilterList);
    ImGui::SameLine();
    if (ImGui::ChipSelector("##ViewType",
                            &view_type_,
                            {
                                {ViewType::ALL, "All"},
                                {ViewType::RECENT, "Recent"},
                                {ViewType::STARRED, "Starred"},
                                {ViewType::BASE, "Base"},
                            })) {
      app_.local_store().PutInt(GetViewTypeKey(type_, is_selector_), (int)view_type_);
    }

    if (num_matches_ > 0) {
      ImGui::SameLine();
      std::string maybe_s = num_matches_ == 1 ? "" : "s";
      ImGui::TextDisabled(std::format(
          "   {} {}{}", num_matches_, absl::AsciiStrToLower(ObjectTypeToString(type_)), maybe_s));
    }

    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float search_icon_right_size = spacing + ImGui::GetDefaultCharSizeX() * 2;

    if (ImGui::TreeNode("Advanced filters")) {
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Bundle"));
      ImGui::SameLine();
      ImGui::SimpleDropdown("BundlePicker", &bundle_name_filter_, bundle_names_.value());
      if (!bundle_name_filter_.empty()) {
        ImGui::SameLine();
        if (ImGui::ClearButton("ClearBundle")) {
          bundle_name_filter_ = "";
        }
      }

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Shot type"));
      ImGui::SameLine();
      ImGui::SimpleTypeDropdown("ShotTypeFilterDropdown", &shot_type_filter_, shot_types_);
      if (shot_type_filter_ != ShotType::TYPE_NOT_SET) {
        ImGui::SameLine();
        if (ImGui::ClearButton("ClearShotType")) {
          shot_type_filter_ = ShotType::TYPE_NOT_SET;
        }
      }

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Target score"));
      ImGui::SameLine();
      ImGui::SimpleTypeDropdown(
          "TargetScoreFilterDropdown", &target_score_filter_, kTargetScoreFilters);
      if (target_score_filter_ != TargetScoreFilter::NO_FILTER) {
        ImGui::SameLine();
        if (ImGui::ClearButton("ClearTargetScore")) {
          target_score_filter_ = TargetScoreFilter::NO_FILTER;
        }
      }

      ImGui::TreePop();
    } else {
      ClearAdvancedFilters();
    }

    float available_width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(available_width - search_icon_right_size);
    ImGui::InputTextWithHint("##SearchInput", icons::kSearch, &search_text_);
    ImGui::SameLine();
    if (search_text_.size() > 0) {
      if (ImGui::ClearButton("ClearSearchText")) {
        search_text_ = "";
      }
    } else {
      ImGui::HelpMarker(
          "Filter results using case insensitive prefix search on words within the name. Spaces "
          "between search terms means name must match all provided terms. This allows further "
          "refining results by adding more terms/filters. \"cli\" matches \"AF Clicking\" and "
          "\"AF MyClick\" as camel case words get split when matching. \"cli m\" matches \"AF "
          "MyClick\". Use search terms like \"-cli\" to exclude matches.");
    }

    ImGui::BeginChild("SearchContent");
    auto child_cleanup = absl::MakeCleanup([] { ImGui::EndChild(); });

    auto new_names = GetNames();
    if (new_names != all_names_) {
      all_names_ = new_names;
      UpdateFilteredNames();
    } else if (search_text_ != handled_search_text_ || !AdvancedFiltersHandled()) {
      UpdateFilteredNames();
    }

    ImGuiListClipper clipper;
    if (filtered_names_indices_) {
      clipper.Begin(filtered_names_indices_->size());
      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
          int names_i = (*filtered_names_indices_)[i];
          const std::string& name = all_names_.get(names_i);
          ImGui::IdGuard cid(name, names_i);
          DrawItem(name, all_names_.IsReadonly(i), result);
        }
      }
    } else {
      clipper.Begin(all_names_.size());
      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
          const std::string& name = all_names_.get(i);
          ImGui::IdGuard cid(name, i);
          DrawItem(name, all_names_.IsReadonly(i), result);
        }
      }
    }
  }

 private:
  void DrawItem(const std::string& name, bool is_readonly_object, Result* result) {
    if (!ItemExists(name)) {
      DrawMissingItem(name);
      return;
    }

    bool is_scenario = type_ == ObjectType::SCENARIO;
    bool is_playlist = type_ == ObjectType::PLAYLIST;

    if (ImGui::Button(name)) {
      result->selected_object_name = name;
    }
    ImGui::HelpTooltip(name, 0.6);

    if (!is_selector_) {
      const char* menu_id = "ItemMenu";
      if (ImGui::BeginPopupContextItem(menu_id)) {
        auto resource_name = ResourceName::Parse(name);
        bool is_readonly = is_readonly_object ||
                           app_.bundle_manager().IsBundleReadonly(resource_name.bundle_name());

        if (!is_readonly) {
          if (ImGui::Selectable(std::format("{} Edit", icons::kEdit))) {
            result->edit_object_name = name;
          }
        }

        if (ImGui::Selectable(std::format("{} Copy", icons::kContentCopy))) {
          result->copy_object_name = name;
        }

        if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
          select_variation_dialog_.NotifyOpen(name);
        }

        if (ImGui::Selectable(std::format("{} Recents", icons::kClose))) {
          app_.history_manager().DeleteRecentView(type_, name);
        }

        if (is_scenario) {
          if (ImGui::BeginMenu(Tr("Add to"))) {
            DrawAddScenarioToPlaylistMenu(name);
            ImGui::EndMenu();
          }
          if (ImGui::BeginMenu(Tr("Advanced"))) {
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
    }

    ImGui::SameLine();
    DrawStarButton(type_, name, app_);
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
    if (ImGui::Selectable(Tr("View stats"))) {
      app_.PushNextScreen(
          CreateStatsScreen(name, app_.stats_manager().GetLatestRunId(name), false));
    }
    if (ImGui::Selectable(Tr("Copy as reference"))) {
      ScenarioEditorOptions opts;
      opts.scenario_name = name;
      opts.is_new_copy = true;
      opts.copy_as_reference = true;
      app_.PushNextScreen(CreateScenarioEditorScreen(opts));
    }
    if (ImGui::Selectable(Tr("Create levels playlist"))) {
      result->create_level_playlist_for_scenario = name;
    }
  }

  void DrawMissingItem(const std::string& name) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text(name);
    if (view_type_ == ViewType::RECENT) {
      ImGui::SameLine();
      if (ImGui::IconButton(icons::kDelete)) {
        app_.history_manager().DeleteRecentView(type_, name);
      }
      ImGui::HelpTooltip(Tr("Delete from recents"));
    }
    if (view_type_ == ViewType::STARRED) {
      ImGui::SameLine();
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

  AllNames GetNames() {
    AllNames all_names;
    if (view_type_ == ViewType::RECENT) {
      all_names.names1 = app_.history_manager().GetCachedRecentNames(type_);
      return all_names;
    }
    if (view_type_ == ViewType::STARRED) {
      auto items = app_.labels_manager().ListStarredItems(type_);
      all_names.names1 = std::shared_ptr<std::vector<std::string>>(items, &items->items);
      return all_names;
    }
    switch (type_) {
      case ObjectType::SCENARIO:
        all_names.names1 = app_.scenario_manager().scenario_names();
        return all_names;
      case ObjectType::PLAYLIST:
        all_names.names1 = app_.playlist_manager().playlist_names();
        if (view_type_ == ViewType::ALL) {
          all_names.names2 = app_.scenario_manager().scenario_playlist_names();
        }
        return all_names;
      case ObjectType::GUIDE:
        all_names.names1 = app_.guide_manager().guide_names();
        if (view_type_ == ViewType::ALL) {
          all_names.names2 = app_.scenario_manager().scenario_guide_names();
          all_names.names3 = app_.playlist_manager().playlist_guide_names();
        }
        return all_names;
      case ObjectType::THEME:
      case ObjectType::CROSSHAIR:
        break;
    }
    assert(false && "Unsupported object type");
    return all_names;
  }

  bool HasAdvancedFilters() {
    return !bundle_name_filter_.empty() || HasAdvancedScenarioFilters();
  }

  // Filters that require reading the scenario definition.
  bool HasAdvancedScenarioFilters() {
    return target_score_filter_ != TargetScoreFilter::NO_FILTER || shot_type_filter_;
  }

  bool AdvancedFiltersHandled() {
    return bundle_name_filter_ == handled_bundle_name_filter_ &&
           target_score_filter_ == handled_target_score_filter_ &&
           shot_type_filter_ == handled_shot_type_filter_;
  }

  void ClearAdvancedFilters() {
    bundle_name_filter_ = "";
    target_score_filter_ = TargetScoreFilter::NO_FILTER;
    shot_type_filter_ = ShotType::TYPE_NOT_SET;
  }

  void UpdateFilteredNames() {
    handled_search_text_ = search_text_;
    handled_bundle_name_filter_ = bundle_name_filter_;
    handled_target_score_filter_ = target_score_filter_;
    handled_shot_type_filter_ = shot_type_filter_;
    if (search_text_.empty() && !HasAdvancedFilters()) {
      // All match. Clear the filter.
      filtered_names_indices_ = {};
      num_matches_ = all_names_.size();
      return;
    }

    auto search_words = GetSearchWords(search_text_);
    if (!filtered_names_indices_) {
      filtered_names_indices_ = std::vector<int>{};
    }
    auto& indices = *filtered_names_indices_;
    indices.clear();
    indices.reserve(all_names_.size());

    for (int i = 0; i < all_names_.size(); ++i) {
      const std::string& name = all_names_.get(i);
      if (!search_text_.empty() && !StringMatchesSearch(name, search_words)) {
        continue;
      }
      if (!MatchesAdvancedFilter(name)) {
        continue;
      }
      indices.push_back(i);
    }

    num_matches_ = indices.size();
  }

  bool MatchesAdvancedFilter(const std::string& name) {
    if (!bundle_name_filter_.empty() && !name.starts_with(bundle_name_filter_ + " ")) {
      return false;
    }
    if (!HasAdvancedScenarioFilters()) {
      return true;
    }

    if (type_ == ObjectType::SCENARIO) {
      return ScenarioMatchesAdvancedFilter(name);
    }
    if (type_ == ObjectType::PLAYLIST) {
      return PlaylistMatchesAdvancedFilter(name);
    }
    if (type_ == ObjectType::GUIDE) {
      NameInfo name_info = GetNameInfo(name);
      if (name_info.is_cm360_variations && name_info.is_level_variations) {
        name_info.is_cm360_variations = false;
        return PlaylistMatchesAdvancedFilter(name_info.GetFullName());
      }
      return false;
    }

    return true;
  }

  bool PlaylistMatchesAdvancedFilter(const std::string& name) {
    auto playlist = app_.playlist_manager().GetPlaylist(name);
    if (!playlist) {
      return false;
    }
    if (!playlist->def().has_levels()) {
      return false;
    }
    return ScenarioMatchesAdvancedFilter(playlist->def().levels().base_scenario(),
                                         playlist->def().levels().target_score());
  }

  bool ScenarioMatchesAdvancedFilter(const std::string& name, float playlist_target_score = 0.0f) {
    auto scenario = app_.scenario_manager().GetEvaluatedScenarioDef(name);
    if (!scenario) {
      return false;
    }

    if (!TargetScoreFilterMatches(
            std::max(scenario->score_targets().target_score(), playlist_target_score))) {
      return false;
    }

    if (shot_type_filter_ != ShotType::TYPE_NOT_SET) {
      if (scenario->shot_type().type_case() != shot_type_filter_) {
        return false;
      }
    }
    return true;
  }

  bool TargetScoreFilterMatches(float target_score) {
    if (target_score_filter_ == TargetScoreFilter::NO_FILTER) {
      return true;
    }
    bool expected_has_target_score =
        target_score_filter_ == TargetScoreFilter::HAS_TARGET_SCORE ? true : false;
    return expected_has_target_score == (target_score > 0);
  }

  Application& app_ = GetUiApp();
  bool is_selector_;
  std::string search_text_;
  std::string handled_search_text_;
  TargetScoreFilter target_score_filter_ = TargetScoreFilter::NO_FILTER;
  TargetScoreFilter handled_target_score_filter_ = TargetScoreFilter::NO_FILTER;
  ShotType::TypeCase shot_type_filter_ = ShotType::TYPE_NOT_SET;
  ShotType::TypeCase handled_shot_type_filter_ = ShotType::TYPE_NOT_SET;
  const ObjectType type_;
  const std::string type_name_ = ObjectTypeToString(type_);
  ViewType view_type_ = ViewType::ALL;
  ImGui::ConfirmationDialog<std::string> delete_confirmation_dialog_{"DeleteConfirmationDialog"};
  AllNames all_names_;
  std::optional<std::vector<int>> filtered_names_indices_;
  SelectVariationDialog select_variation_dialog_{"ObjectBrowserVariationDialog"};
  Lazy<std::vector<std::string>> bundle_names_;
  std::string bundle_name_filter_;
  std::string handled_bundle_name_filter_;
  i64 num_matches_ = 0;

  std::vector<std::pair<ShotType::TypeCase, std::string>> shot_types_{{ShotType::TYPE_NOT_SET, ""}};
};

}  // namespace

std::unique_ptr<ObjectBrowser> CreateObjectBrowser(ObjectType type, bool is_selector) {
  return std::make_unique<ObjectBrowserImpl>(type, is_selector);
}

float GetDefaultObjectBrowserWidth() {
  float char_x = ImGui::GetDefaultCharSizeX();
  return char_x * 50;
}

void DrawStarButton(ObjectType type, const std::string& name, Application& app) {
  if (app.labels_manager().IsStarred(type, name)) {
    if (ImGui::IconButton(icons::kStar)) {
      app.labels_manager().UnstarItem(type, name);
    }
  } else {
    if (ImGui::IconButton(icons::kStarOutline)) {
      app.labels_manager().StarItem(type, name);
    }
  }
}

}  // namespace aim
