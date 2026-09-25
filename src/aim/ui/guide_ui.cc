#include "guide_ui.h"

#include <deque>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/container/linked_hash_map.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/lazy_cache.h"
#include "aim/common/mat_icons.h"
#include "aim/common/object_type.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/guide_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/proto/guide.pb.h"
#include "aim/ui/guide_editor_screen.h"
#include "aim/ui/object_browser.h"
#include "aim/ui/playlist_ui.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace aim {
namespace {

constexpr int kMaxHistorySize = 100;

void CopyGuide(const std::string guide_name, Application& app) {
  std::string bundle_name = GetBundleName(guide_name);
  if (app.bundle_manager().IsBundleReadonly(bundle_name)) {
    bundle_name = app.bundle_manager().GetDefaultWritableBundleName();
  }
  std::string new_guide_name = app.guide_manager().QuickCopyGuide(guide_name, bundle_name);
  app.bundle_manager().SaveDirtyBundles();
  app.guide_manager().SetCurrentGuide(new_guide_name);
}

class GuideViewer {
 public:
  struct Result {
    std::optional<std::string> selected_guide;
    bool current_playlist_selected = false;
  };

  void Draw(const GuideItem& guide_item, Result* result) {
    if (guide_item.name != guide_name_) {
      guide_name_ = guide_item.name;
      guide_name_info_ = GetNameInfo(guide_name_);
      highest_level_cache_.Clear();
    }
    const GuideDef& guide = guide_item.def;
    ImGui::LoopId loop_id;
    for (const auto& section : guide.sections()) {
      auto lid = loop_id.Get("Section");
      DrawSection(section, result);
    }
  }

  void DrawSection(const GuideSection& section, Result* result) {
    if (!section.text().empty()) {
      ImGui::TextWrapped(section.text());
    }
    if (section.playlists_size() > 0) {
      DrawPlaylists(section, result);
    }
    if (section.guides_size() > 0) {
      DrawGuides(section, result);
    }
  }

  void DrawPlaylists(const GuideSection& section, Result* result) {
    ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Borders;
    if (!ImGui::BeginTable("Playlists", 2, flags)) {
      return;
    }
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

    // TODO: Only add this column if there is actually a level to show
    float level_width = ImGui::CalcTextSize("L22.5_").x;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, level_width);

    LazyCacheOptions cache_opts;
    cache_opts.num_to_load = 1;
    cache_opts.cache_stale_time_micros = cache_refresh_time_micros_;
    highest_level_cache_.LoadSomeItems(
        cache_opts, std::bind_front(&GuideViewer::GetHighestLevelForPlaylist, this));

    ImGui::LoopId loop_id;
    for (const std::string& unmerged_playlist : section.playlists()) {
      NameInfo playlist_name_info = GetNameInfo(unmerged_playlist);
      playlist_name_info.MergeDynamicSuffixes(guide_name_info_);
      std::string playlist = playlist_name_info.GetFullName();

      ImGui::TableNextRow();
      auto lid = loop_id.Get("Playlist");
      ImGui::TableNextColumn();
      bool is_selected = playlist == app_.playlist_manager().current_playlist_name();
      if (is_selected) {
        result->current_playlist_selected = true;
      }
      if (ImGui::Selectable(std::format("{} {}", icons::kList, playlist), is_selected)) {
        app_.playlist_manager().SetCurrentPlaylist(playlist);
      }

      std::optional<float> highest_level = highest_level_cache_.Get(playlist);
      if (highest_level) {
        ImGui::TableNextColumn();
        std::string text =
            std::format("L{}{}", MaybeIntToString(*highest_level, 1), icons::kVerified);
        ImGui::TextAligned(1.0f, -FLT_MIN, "%s", text.c_str());
      }
    }

    ImGui::EndTable();
  }

  void DrawGuides(const GuideSection& section, Result* result) {
    ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Borders;
    if (!ImGui::BeginTable("Guides", 1, flags)) {
      return;
    }
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

    ImGui::LoopId loop_id;
    for (const std::string& unmerged_guide : section.guides()) {
      NameInfo guide_info = GetNameInfo(unmerged_guide);
      guide_info.MergeDynamicSuffixes(guide_name_info_);
      std::string guide = guide_info.GetFullName();

      ImGui::TableNextRow();
      auto lid = loop_id.Get("Guide");
      ImGui::TableNextColumn();
      if (ImGui::Selectable(std::format("{} {}", icons::kMap, guide))) {
        result->selected_guide = guide;
      }
    }

    ImGui::EndTable();
  }

  std::optional<float> GetHighestLevelForPlaylist(const std::string& playlist_name) {
    auto maybe_playlist = app_.playlist_manager().GetPlaylist(playlist_name);
    if (!maybe_playlist) {
      return {};
    }
    auto& playlist = *maybe_playlist;
    if (!playlist.def().has_levels()) {
      return {};
    }
    NameInfo playlist_name_info = GetNameInfo(playlist_name);
    NameInfo scenario_name_info = GetNameInfo(playlist.def().levels().base_scenario());
    scenario_name_info.level = {};
    scenario_name_info.MergeDynamicSuffixes(playlist_name_info);
    std::string base_name = scenario_name_info.GetFullName();
    auto maybe_scenario = app_.scenario_manager().GetEvaluatedScenarioDef(base_name);
    if (!maybe_scenario) {
      return {};
    }
    float target_score = maybe_scenario->score_targets().target_score();
    if (target_score > 0) {
      return app_.stats_manager().GetHighestCompleteScenarioLevel(base_name, target_score);
    }
    return {};
  }

 private:
  Application& app_ = GetUiApp();

  LazyCache<float> highest_level_cache_;
  i64 cache_refresh_time_micros_ = SecondsToMicros(0.6);
  std::string guide_name_;
  NameInfo guide_name_info_;
};

class GuidesComponentImpl : public GuidesComponent {
 public:
  void Show() override {
    bool go_back = false;
    ImGui::IdGuard cid("Guides");

    std::string updated_guide_variation_name;
    if (select_variation_dialog_.Draw(&updated_guide_variation_name)) {
      app_.guide_manager().SetCurrentGuide(updated_guide_variation_name);
    }

    {
      const std::string& current_guide_name = app_.guide_manager().current_guide_name();
      if (!current_guide_name.empty()) {
        if (guide_history_.empty()) {
          guide_history_.push_back(current_guide_name);
        } else if (guide_history_.back() != current_guide_name) {
          guide_history_.push_back(current_guide_name);
        }
        if (guide_history_.size() > kMaxHistorySize) {
          guide_history_.pop_front();
        }
      }
    }

    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("GuideColumns", 3, flags)) {
      ImGui::TableNextColumn();
      ImGui::BeginChild("GuideBrowserColumn");
      ImGui::Spacing();
      if (ImGui::Button(std::format("{} Guide", icons::kAdd))) {
        GuideEditorOptions opts;
        opts.name = "";
        opts.is_new_guide = true;
        app_.PushNextScreen(CreateGuideEditorScreen(opts));
      }

      ImGui::SpacedSeparator();

      ObjectBrowser::Result result;
      browser_->Draw(&result);
      if (result.selected_object_name) {
        app_.guide_manager().SetCurrentGuide(*result.selected_object_name);
        app_.history_manager().UpdateRecentView(ObjectType::GUIDE, *result.selected_object_name);
      }
      if (result.edit_object_name) {
        GuideEditorOptions opts;
        opts.name = *result.edit_object_name;
        app_.PushNextScreen(CreateGuideEditorScreen(opts));
      }
      if (result.copy_object_name) {
        CopyGuide(*result.copy_object_name, app_);
      }

      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("GuideColumn");

      bool current_playlist_selected = false;
      DrawCurrentGuidePanel(&go_back, &current_playlist_selected);

      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("PlaylistColumn");

      if (current_playlist_selected) {
        auto playlist_run = app_.playlist_manager().GetCurrentRun();
        if (playlist_run) {
          PlaylistComponent::Options options;
          options.is_playlist_screen = false;
          playlist_component_->Show(playlist_run, options);
        }
      }

      ImGui::EndChild();

      ImGui::EndTable();
    }

    if (go_back && guide_history_.size() > 1) {
      guide_history_.pop_back();
      app_.guide_manager().SetCurrentGuide(guide_history_.back());
    }
  }

 private:
  void DrawCurrentGuidePanel(bool* go_back, bool* current_playlist_selected) {
    std::optional<GuideItem> guide = app_.guide_manager().GetCurrentGuide();
    if (!guide) {
      return;
    }
    ImGui::Spacing();
    if (guide_history_.size() > 1) {
      ImGui::AlignTextToFramePadding();
      if (ImGui::SelectableButton(icons::kArrowBack)) {
        *go_back = true;
      }
      ImGui::HelpTooltip("Back to last guide");
      ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text(guide->name);

    const char* menu_id = "GuideMenu";
    if (ImGui::BeginPopupContextItem(menu_id)) {
      bool is_readonly = app_.bundle_manager().IsBundleReadonly(GetBundleName(guide->name));
      if (!is_readonly) {
        if (ImGui::Selectable(std::format("{} Edit", icons::kEdit))) {
          GuideEditorOptions opts;
          opts.name = guide->name;
          app_.PushNextScreen(CreateGuideEditorScreen(opts));
        }
      }
      if (ImGui::Selectable(std::format("{} Copy", icons::kContentCopy))) {
        CopyGuide(guide->name, app_);
      }
      if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
        select_variation_dialog_.NotifyOpen(guide->name);
      }
      ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::MenuButton()) {
      ImGui::OpenPopup(menu_id);
    }

    ImGui::SpacedSeparator();

    GuideViewer::Result result;
    viewer_.Draw(*guide, &result);
    if (result.selected_guide) {
      app_.guide_manager().SetCurrentGuide(*result.selected_guide);
    }
    *current_playlist_selected = result.current_playlist_selected;
  }

  Application& app_ = GetUiApp();

  std::unique_ptr<ObjectBrowser> browser_ = CreateObjectBrowser(ObjectType::GUIDE);
  std::unique_ptr<PlaylistComponent> playlist_component_ = CreatePlaylistComponent();
  GuideViewer viewer_;
  std::deque<std::string> guide_history_;
  SelectVariationDialog select_variation_dialog_{"CurrentGuideVariation"};
};

}  // namespace

std::unique_ptr<GuidesComponent> CreateGuidesComponent() {
  return std::make_unique<GuidesComponentImpl>();
}

}  // namespace aim
