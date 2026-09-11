#include "guide_ui.h"

#include <deque>
#include <string>

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/object_type.h"
#include "aim/common/resource_name.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/guide_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/proto/guide.pb.h"
#include "aim/ui/guide_editor_screen.h"
#include "aim/ui/object_browser.h"
#include "aim/ui/playlist_ui.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"

namespace aim {
namespace {

constexpr int kMaxHistorySize = 100;

class AddGuideDialog {
 public:
  explicit AddGuideDialog(const std::string& id) : id_(id) {}

  void NotifyOpen() {
    open_ = true;
  }

  bool Draw(Application& app, std::string* guide_name) {
    ImGui::IdGuard cid("AddGuideDialogContent");
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
          auto taken_names = app.guide_manager().GetAllRelativeNamesInBundle(name_.bundle_name());
          *name_.mutable_relative_name() = MakeUniqueName(name_.relative_name(), taken_names);
          app.guide_manager().UpdateGuide(name_.full_name(), GuideDef());
          app.history_manager().UpdateRecentView(ObjectType::GUIDE, name_.full_name());
          *guide_name = name_.full_name();
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
      name_.set(kUserBundleName, "New guide");
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

class GuideViewer {
 public:
  struct Result {
    std::optional<std::string> selected_guide;
  };

  void Draw(const GuideItem& guide_item, Result* result) {
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
      DrawPlaylists(section);
    }
    if (section.guides_size() > 0) {
      DrawGuides(section, result);
    }
  }

  void DrawPlaylists(const GuideSection& section) {
    ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Borders;
    if (!ImGui::BeginTable("Playlists", 2, flags)) {
      return;
    }
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

    // TODO: Only add this column if there is actually a level to show
    float level_width = ImGui::CalcTextSize("L22.5_").x;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, level_width);

    ImGui::LoopId loop_id;
    for (const std::string& playlist : section.playlists()) {
      ImGui::TableNextRow();
      auto lid = loop_id.Get("Playlist");
      ImGui::TableNextColumn();
      bool is_selected = playlist == app_.playlist_manager().current_playlist_name();
      if (ImGui::Selectable(std::format("{} {}", icons::kList, playlist), is_selected)) {
        app_.playlist_manager().SetCurrentPlaylist(playlist);
      }
      auto maybe_playlist = app_.playlist_manager().GetPlaylist(playlist);
      if (maybe_playlist) {
        auto highest_complete_level = app_.playlist_manager().GetHighestCompleteLevel(
            *maybe_playlist, app_.scenario_manager(), app_.stats_manager());
        if (highest_complete_level) {
          ImGui::TableNextColumn();
          ImGui::TextFmt("L{}{}", MaybeIntToString(*highest_complete_level, 1), icons::kVerified);
        }
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
    for (const std::string& guide : section.guides()) {
      ImGui::TableNextRow();
      auto lid = loop_id.Get("Guide");
      ImGui::TableNextColumn();
      if (ImGui::Selectable(std::format("{} {}", icons::kMap, guide))) {
        result->selected_guide = guide;
      }
    }

    ImGui::EndTable();
  }

 private:
  Application& app_ = GetUiApp();
};

class GuidesComponentImpl : public GuidesComponent {
 public:
  void Show() override {
    bool go_back = false;
    ImGui::IdGuard cid("Guides");
    std::string added_guide_name;
    if (add_dialog_.Draw(app_, &added_guide_name)) {
      app_.bundle_manager().SaveDirtyBundles();

      app_.guide_manager().SetCurrentGuide(added_guide_name);

      GuideEditorOptions opts;
      opts.name = added_guide_name;
      app_.PushNextScreen(CreateGuideEditorScreen(opts));
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
        add_dialog_.NotifyOpen();
      }

      ImGui::SpacedSeparator();

      ObjectBrowser::Result result;
      browser_->Draw(&result);
      if (result.selected_object_name) {
        app_.guide_manager().SetCurrentGuide(*result.selected_object_name);
        app_.history_manager().UpdateRecentView(ObjectType::GUIDE, *result.selected_object_name);
      }

      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("GuideColumn");

      std::optional<GuideItem> guide = app_.guide_manager().GetCurrentGuide();
      if (guide) {
        ImGui::Spacing();
        if (guide_history_.size() > 1) {
          ImGui::AlignTextToFramePadding();
          if (ImGui::Button(icons::kArrowBack)) {
            go_back = true;
          }
          ImGui::HelpTooltip("Back to last guide");
          ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::Text(guide->name);
        ImGui::SameLine();
        if (ImGui::Button(icons::kEdit)) {
          GuideEditorOptions opts;
          opts.name = guide->name;
          app_.PushNextScreen(CreateGuideEditorScreen(opts));
        }

        ImGui::SpacedSeparator();

        GuideViewer::Result result;
        viewer_.Draw(*guide, &result);
        if (result.selected_guide) {
          app_.guide_manager().SetCurrentGuide(*result.selected_guide);
        }
      }

      ImGui::EndChild();

      ImGui::TableNextColumn();
      ImGui::BeginChild("PlaylistColumn");

      if (guide) {
        auto playlist_run = GetPlaylistRunIfInGuide(guide->def);
        if (playlist_run) {
          playlist_component_->Show(playlist_run, /*is_playlist_screen*/ false);
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
  std::shared_ptr<PlaylistRun> GetPlaylistRunIfInGuide(const GuideDef& guide) {
    auto current_run = app_.playlist_manager().GetCurrentRun();
    if (!current_run) {
      return {};
    }
    for (const auto& section : guide.sections()) {
      for (const std::string& playlist : section.playlists()) {
        if (playlist == current_run->playlist.name) {
          return current_run;
        }
      }
    }
    return {};
  }

  Application& app_ = GetUiApp();

  std::unique_ptr<ObjectBrowser> browser_ = CreateObjectBrowser(ObjectType::GUIDE);
  std::unique_ptr<PlaylistComponent> playlist_component_ = CreatePlaylistComponent();
  AddGuideDialog add_dialog_{"AddGuideDialog"};
  GuideViewer viewer_;
  std::deque<std::string> guide_history_;
};

}  // namespace

std::unique_ptr<GuidesComponent> CreateGuidesComponent() {
  return std::make_unique<GuidesComponentImpl>();
}

}  // namespace aim
